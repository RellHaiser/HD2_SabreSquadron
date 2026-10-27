"""Edit the holster table of Hidden & Dangerous 2 (g_holsterModels) in an executable.

Each record of the table (src/game/human_equipment.cpp) is 12 bytes, followed by the
table's byte size (g_holsterModelsSize):
    const char *model;   // uniform model file name without path and extension
    int set;             // g_holsterSets index, -1: none
    int nation;          // E_HolsterNation

The table is found by its shape, so this works on the unpacked game executables
(HD2_SabreSquadron_unpacked*.exe) and the dedicated server; the protected original
keeps it encrypted.

Model names are written over their old string when it belongs to the table alone.
Strings the game also uses elsewhere ("EN06" and "US03" are compared in HolsterJoint,
"NA01" and "GE03" are used by other code) are left alone: a renamed entry gets a new
8-byte string in the unused tail of .rdata instead (the section's virtual size grows
to cover it). Saving over the opened file first copies it to <file>.bak once.

Usage: python holster_patcher.py [EXE]
"""
import os
import shutil
import struct
import sys
import tkinter as tk
from dataclasses import dataclass
from tkinter import filedialog, messagebox, ttk
from tkinter import font as tkfont

RECORD = struct.Struct("<Iii")
NAME_MAX = 7                    # characters; the 8-byte string slot keeps the terminator
SLOT = NAME_MAX + 1
NAME_FORBIDDEN = '\\/:*?"<>|'   # model names are file names

# g_holsterSets (used when the names in the executable are not readable).
SET_NAMES = ["def_holster_set", "en_vyb01", "en_vyb02", "en_vyb03", "ge_vyb01",
             "it_vyb01", "ja_vyb", "ru_vyb", "us_vyb", "us_vyb2"]
# E_HolsterNation
NATIONS = ["British", "German", "Russian", "Japanese", "Italian", "American", "Civilian"]

SCN_CNT_CODE = 0x00000020
# Sections whose unused file tail may take new strings, in order of preference.
CAVE_SECTIONS = (".rdata", ".data")


class PatchError(Exception):
    pass


def u16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def ref_count(data, va):
    """How often the address appears in the file (data pointers, code operands)."""
    return data.count(struct.pack("<I", va))


@dataclass
class Section:
    name: str
    header: int                 # file offset of the section header
    rva: int
    vsize: int
    roff: int
    rsize: int
    chars: int


class PeImage:
    def __init__(self, data):
        if data[:2] != b"MZ":
            raise PatchError("Not an executable.")
        pe = u32(data, 0x3C)
        if data[pe:pe + 4] != b"PE\0\0" or u16(data, pe + 4) != 0x14C:
            raise PatchError("Not a 32-bit Windows executable.")
        opt = pe + 24
        if u16(data, opt) != 0x10B:
            raise PatchError("Not a PE32 executable.")
        self.base = u32(data, opt + 28)
        self.image_size = u32(data, opt + 56)
        self.checksum_off = opt + 64
        headers = opt + u16(data, pe + 20)
        self.sections = []
        for i in range(u16(data, pe + 6)):
            off = headers + 40 * i
            name, vsize, rva, rsize, roff = struct.unpack_from("<8sIIII", data, off)
            self.sections.append(Section(name.rstrip(b"\0").decode("latin-1"), off, rva,
                                         vsize, roff, rsize, u32(data, off + 36)))

    def section_end(self, sec):
        """End RVA of the file data of the section that is mapped into memory."""
        following = [s.rva for s in self.sections if s.rva > sec.rva]
        return sec.rva + min(sec.rsize, min(following, default=self.image_size) - sec.rva)

    def offset(self, va):
        rva = va - self.base
        for sec in self.sections:
            if sec.rva <= rva < self.section_end(sec):
                return sec.roff + rva - sec.rva
        return None

    def address(self, offset):
        for sec in self.sections:
            if sec.roff <= offset < sec.roff + sec.rsize:
                return self.base + sec.rva + offset - sec.roff
        return None


def pe_checksum(data, field):
    words = bytearray(data)
    words[field:field + 4] = b"\0\0\0\0"
    if len(words) % 2:
        words.append(0)
    total = sum(memoryview(words).cast("H"))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return total + len(data)


@dataclass
class Record:
    name: str
    set: int
    nation: int
    ptr: int
    shared: bool                # the string has other users, so a rename moves it
    capacity: int               # bytes the string may use in place (with the terminator)


class HolsterTable:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.pe = PeImage(self.data)
        self.offset, count = self._find_table()
        self.va = self.pe.address(self.offset)
        self.set_names = self._read_set_names()
        self.records = []
        for i in range(count):
            ptr, set_, nation = RECORD.unpack_from(self.data, self.offset + RECORD.size * i)
            name = self.read_name(ptr)
            shared = ref_count(self.data, ptr) != 1
            self.records.append(Record(name, set_, nation, ptr, shared,
                                       self._capacity(ptr, len(name))))

    def read_name(self, ptr):
        off = self.pe.offset(ptr)
        if off is None:
            return None
        raw = self.data[off:off + 32]
        end = raw.find(b"\0")
        if end < 1 or not all(0x20 < b < 0x7F for b in raw[:end]):
            return None
        return raw[:end].decode("ascii")

    def _find_table(self):
        """The longest run of plausible records followed by its own byte size."""
        best = None
        for sec in self.pe.sections:
            if sec.chars & SCN_CNT_CODE or not sec.rsize:
                continue
            valid = []
            end = min(sec.roff + sec.rsize, len(self.data)) - RECORD.size
            for off in range(sec.roff, end + 1, 4):
                ptr, set_, nation = RECORD.unpack_from(self.data, off)
                if -1 <= set_ <= 0xFF and 0 <= nation <= 0xFF and self.read_name(ptr):
                    valid.append(off)
            run = {}
            for off in reversed(valid):
                run[off] = 1 + run.get(off + RECORD.size, 0)
            for off, n in run.items():
                size_off = off + RECORD.size * n
                if (size_off + 4 <= len(self.data) and u32(self.data, size_off) == RECORD.size * n
                        and (best is None or n > best[1])):
                    best = (off, n)
        if best is None or best[1] < 8:
            raise PatchError("The holster table was not found.\n\n"
                             "Use an unpacked executable (the protected original keeps it encrypted).")
        return best

    def _read_set_names(self):
        off = self.offset - 4 * len(SET_NAMES)
        names = [self.read_name(u32(self.data, off + 4 * i)) for i in range(len(SET_NAMES))]
        return names if all(names) else list(SET_NAMES)

    def _capacity(self, ptr, length):
        """String literals are 4-byte aligned and padded with zeros, so a slot holds
        at least 8 bytes; shorter names only extend into bytes nothing points to."""
        off = self.pe.offset(ptr)
        used = length + 1
        if ptr % 4:
            return used
        cap = (used + 3) & ~3
        if any(self.data[off + used:off + cap]):
            return used
        if (cap < SLOT and not any(self.data[off + cap:off + SLOT])
                and ref_count(self.data, ptr + cap) == 0):
            cap = SLOT
        return cap

    def _allocate(self, out, raw):
        """Put a name into the unused file tail of a data section and return its address."""
        for name in CAVE_SECTIONS:
            for sec in self.pe.sections:
                if sec.name != name:
                    continue
                vsize = u32(out, sec.header + 8)
                rva = sec.rva + ((vsize + SLOT - 1) & ~(SLOT - 1))
                while rva + SLOT <= self.pe.section_end(sec):
                    off = sec.roff + rva - sec.rva
                    va = self.pe.base + rva
                    if not any(out[off:off + SLOT]) and ref_count(out, va) == 0:
                        out[off:off + SLOT] = raw.ljust(SLOT, b"\0")
                        struct.pack_into("<I", out, sec.header + 8, rva + SLOT - sec.rva)
                        return va
                    rva += SLOT
        raise PatchError(f'No free space for the new string "{raw.decode()}".')

    def build(self, values):
        """The executable with the records set to values: [(name, set, nation)]."""
        out = bytearray(self.data)
        for i, (rec, (name, set_, nation)) in enumerate(zip(self.records, values)):
            ptr = rec.ptr
            if name != rec.name:
                raw = name.encode("ascii")
                if not rec.shared and len(raw) < rec.capacity:
                    off = self.pe.offset(ptr)
                    out[off:off + rec.capacity] = raw.ljust(rec.capacity, b"\0")
                else:
                    ptr = self._allocate(out, raw)
            RECORD.pack_into(out, self.offset + RECORD.size * i, ptr, set_, nation)
        if u32(self.data, self.pe.checksum_off):
            struct.pack_into("<I", out, self.pe.checksum_off, pe_checksum(out, self.pe.checksum_off))
        return out


def valid_name_edit(new, old):
    if len(new) > NAME_MAX and len(new) >= len(old):
        return False        # (an original longer name may still be shortened)
    return all(0x20 < ord(c) < 0x7F and c not in NAME_FORBIDDEN for c in new)


class App(tk.Tk):
    def __init__(self, path=None):
        super().__init__()
        self.title("HD2 Holster Patcher")
        self.geometry("780x680")
        self.minsize(560, 300)
        self.table = None
        self.rows = []
        self.set_labels = []
        self.nation_labels = []
        self.header_font = tkfont.nametofont("TkDefaultFont").copy()
        self.header_font.configure(weight="bold")

        bar = ttk.Frame(self, padding=(6, 6, 6, 0))
        bar.pack(fill="x")
        ttk.Button(bar, text="Open...", command=self.open).pack(side="left")
        self.save_button = ttk.Button(bar, text="Save", command=self.save, state="disabled")
        self.save_button.pack(side="left", padx=(6, 0))
        self.save_as_button = ttk.Button(bar, text="Save as...", command=self.save_as, state="disabled")
        self.save_as_button.pack(side="left", padx=(6, 0))
        self.revert_button = ttk.Button(bar, text="Revert", command=self.revert, state="disabled")
        self.revert_button.pack(side="left", padx=(6, 0))
        ttk.Label(bar, text=f"Model names: up to {NAME_MAX} characters",
                  foreground="gray40").pack(side="right")

        body = ttk.Frame(self, padding=6)
        body.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(body, highlightthickness=0, yscrollincrement=8)
        scroll = ttk.Scrollbar(body, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)
        self.grid_frame = ttk.Frame(self.canvas)
        self.canvas.create_window((0, 0), window=self.grid_frame, anchor="nw")
        self.grid_frame.bind("<Configure>", lambda e: self.canvas.configure(
            scrollregion=self.canvas.bbox("all")))

        self.status = tk.StringVar(value="Open an executable (File: Open...).")
        ttk.Label(self, textvariable=self.status, relief="sunken", anchor="w",
                  padding=(6, 2)).pack(fill="x", side="bottom")

        # The wheel scrolls the table; it must not change the value under the pointer.
        self.unbind_class("TCombobox", "<MouseWheel>")
        self.bind_all("<MouseWheel>", self._on_wheel)
        self.bind_all("<Control-o>", lambda e: self.open())
        self.bind_all("<Control-s>", lambda e: self.save())
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.validate_name = (self.register(valid_name_edit), "%P", "%s")
        if path:
            self.load(path)

    # ---- file handling
    def open(self):
        if not self._discard_ok():
            return
        path = filedialog.askopenfilename(title="Open HD2 executable",
                                          filetypes=[("Executables", "*.exe"), ("All files", "*.*")])
        if path:
            self.load(path)

    def load(self, path):
        try:
            table = HolsterTable(path)
        except (OSError, PatchError) as e:
            messagebox.showerror("Open", f"{path}\n\n{e}")
            return
        self.table = table
        self._build_rows()
        for button in (self.save_button, self.save_as_button, self.revert_button):
            button.configure(state="normal")
        self._update_state()

    def revert(self):
        if self.table and self._discard_ok():
            self.load(self.table.path)

    def save(self):
        if self.table:
            self._write(self.table.path)

    def save_as(self):
        if not self.table:
            return
        path = filedialog.asksaveasfilename(title="Save patched executable", defaultextension=".exe",
                                            initialdir=os.path.dirname(self.table.path),
                                            initialfile=os.path.basename(self.table.path),
                                            filetypes=[("Executables", "*.exe"), ("All files", "*.*")])
        if path:
            self._write(path)

    def _write(self, path):
        values = self._collect()
        if values is None:
            return
        try:
            out = self.table.build(values)
            if os.path.abspath(path) == os.path.abspath(self.table.path) and not os.path.exists(path + ".bak"):
                shutil.copy2(path, path + ".bak")
            tmp = path + ".tmp"
            with open(tmp, "wb") as f:
                f.write(out)
            try:
                os.replace(tmp, path)
            except OSError:
                os.remove(tmp)
                raise
        except (OSError, PatchError) as e:
            messagebox.showerror("Save", f"{path}\n\n{e}")
            return
        top = self.canvas.yview()[0]
        self.load(path)
        self.canvas.yview_moveto(top)
        self.status.set("Saved.  " + self.status.get())

    def close(self):
        if self._discard_ok():
            self.destroy()

    def _discard_ok(self):
        return not self._changed_count() or messagebox.askyesno(
            "Unsaved changes", "Discard the unsaved changes?")

    # ---- the table
    def _build_rows(self):
        for child in self.grid_frame.winfo_children():
            child.destroy()
        self.rows = []
        self.set_labels = ["-1  (none)"] + [f"{i}  {n}" for i, n in enumerate(self.table.set_names)]
        self.nation_labels = [f"{i}  {n}" for i, n in enumerate(NATIONS)]
        for col, text in enumerate(("#", "Model", "Holster set", "Nation", "String")):
            ttk.Label(self.grid_frame, text=text, font=self.header_font).grid(
                row=0, column=col, sticky="w", padx=4, pady=(0, 4))
        for i, rec in enumerate(self.table.records):
            name = tk.StringVar(value=rec.name)
            set_ = tk.StringVar(value=self._label(self.set_labels, rec.set, -1))
            nation = tk.StringVar(value=self._label(self.nation_labels, rec.nation, 0))
            index = ttk.Label(self.grid_frame, text=str(i), width=4, anchor="e")
            index.grid(row=i + 1, column=0, padx=4, sticky="e")
            ttk.Entry(self.grid_frame, textvariable=name, width=12, validate="key",
                      validatecommand=self.validate_name).grid(row=i + 1, column=1, padx=4, pady=1)
            sets = ttk.Combobox(self.grid_frame, textvariable=set_, state="readonly", width=22,
                                values=self._labels(self.set_labels, rec.set, -1))
            sets.grid(row=i + 1, column=2, padx=4, pady=1)
            nations = ttk.Combobox(self.grid_frame, textvariable=nation, state="readonly", width=14,
                                   values=self._labels(self.nation_labels, rec.nation, 0))
            nations.grid(row=i + 1, column=3, padx=4, pady=1)
            note = f"0x{rec.ptr:08X}" + ("  shared: a rename moves it" if rec.shared else "")
            ttk.Label(self.grid_frame, text=note, foreground="gray40").grid(
                row=i + 1, column=4, padx=4, sticky="w")
            for var in (name, set_, nation):
                var.trace_add("write", lambda *_: self._update_state())
            self.rows.append((name, set_, nation, index))
        self.canvas.yview_moveto(0)

    @staticmethod
    def _label(labels, value, first):
        index = value - first
        return labels[index] if 0 <= index < len(labels) else f"{value}  (unknown)"

    def _labels(self, labels, value, first):
        current = self._label(labels, value, first)
        return labels if current in labels else labels + [current]

    def _row_values(self, row):
        name, set_, nation, _ = row
        return name.get(), int(set_.get().split()[0]), int(nation.get().split()[0])

    def _changed_count(self):
        if not self.table:
            return 0
        return sum(self._row_values(row) != (rec.name, rec.set, rec.nation)
                   for row, rec in zip(self.rows, self.table.records))

    def _update_state(self):
        for row, rec in zip(self.rows, self.table.records):
            changed = self._row_values(row) != (rec.name, rec.set, rec.nation)
            row[3].configure(foreground="red3" if changed else "")
        changed = self._changed_count()
        t = self.table
        self.title(f"{'*' if changed else ''}{os.path.basename(t.path)} - HD2 Holster Patcher")
        self.status.set(f"Table 0x{t.va:08X} (file 0x{t.offset:X}), {len(t.records)} entries"
                        + (f", {changed} changed" if changed else "") + f"   -   {t.path}")

    def _collect(self):
        values = [self._row_values(row) for row in self.rows]
        for i, ((name, _, _), rec) in enumerate(zip(values, self.table.records)):
            if name == rec.name:
                continue
            if not name or len(name) > NAME_MAX or not valid_name_edit(name, ""):
                messagebox.showerror("Save", f"Entry {i}: the model name must have 1 to {NAME_MAX} "
                                             f"characters without spaces or {NAME_FORBIDDEN}.")
                return None
        first = {}
        duplicates = []
        for i, (name, _, _) in enumerate(values):
            key = name.lower()
            if key in first:
                duplicates.append(f"{name} (entries {first[key]} and {i})")
            first.setdefault(key, i)
        if duplicates and not messagebox.askokcancel(
                "Save", "The game uses only the first entry of a model:\n\n" + "\n".join(duplicates)):
            return None
        return values

    def _on_wheel(self, event):
        widget = event.widget
        if isinstance(widget, str) or widget.winfo_toplevel() is not self:
            return              # (the list of an open combobox)
        self.canvas.yview_scroll(-3 * (1 if event.delta > 0 else -1), "units")


if __name__ == "__main__":
    App(sys.argv[1] if len(sys.argv) > 1 else None).mainloop()
