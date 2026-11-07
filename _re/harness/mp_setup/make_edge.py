"""Writes the crafted cases the shipped data does not reach, and what `harness roundtrip` must print.

  python make_edge.py DIR

DIR/edge/mpmaplist.txt   GAMESTYLEs in another order, a MAP without a dir (" UNKNOWN "), mode="frontline",
                         OBJECTIVEs of every side and VIP, a lone AIRCRAFTATACK side, the old ALLOWEDITEMS
                         (ids in the text; spaces at the end repeat the last), ZONEGRAPH, an ALLOWEDITEMS of
                         another version (its ITEM, not walked, is met by the list's walk and ends the list:
                         the MAP after it is not read)
DIR/edge/edge.sav        the older chunks (0x1AA, 0x1AD, 0x1AE, 0x1B0, 0x1B4, in that order: each later one
                         overwrites more), a chunk not known, rotations (one out of range), name, password, port
DIR/edge/expected.txt    the dump the harness must print for it
"""
import os
import struct
import sys

LIST = """<MAP_LIST>
<!-- a comment -->
<GAMESTYLE type="cooperative">
  <MAP name="Coop1" dir="Coop1_dir" desc="77">
    <OBJECTIVE allied_text="1" axis_text="2" value="3" type="vip" vip_uniform_id="9" side="axis"/>
    <OBJECTIVE allied_text="4" value="5" side="Allied"/>
    <OBJECTIVE axis_text="6" type="other" side="neither"/>
    <OBJECTIVE/>
    <AIRCRAFTATACK axis="12"/>
    <BALANCED allied="13" axis="14"/>
    <ALLOWEDITEMS>1 22  333
4 </ALLOWEDITEMS>
    <ALLOWEDPRESETS>"A preset"</ALLOWEDPRESETS>
  </MAP>
  <MAP name="NoDir" mode="FRONTLINE"><AIRCRAFTATACK allied="21"/></MAP>
</GAMESTYLE>
<GAMESTYLE type="teamplay">
  <MAP name="Team1" dir="Team1_dir" mode="frontline"><ZONEGRAPH/><ALLOWEDITEMS version="0.2">5</ALLOWEDITEMS></MAP>
  <MAP name="Team2" dir="Team2_dir"><ALLOWEDITEMS version="0.1"><ITEM item_id="7" ammo_num="8"/><OTHER/><ITEM item_num="3"/></ALLOWEDITEMS></MAP>
</GAMESTYLE>
<GAMESTYLE type="hd2multiplayer"><MAP name="Obj1" dir="Obj1_dir" mode="other"/></GAMESTYLE>
<GAMESTYLE type="deathmatch"><MAP name="Dm1" dir="Dm1_dir"/><MAP name="dm1" dir="Dm1_second"/>
  <MAP name="Dm2" dir="Dm2_dir"><ALLOWEDITEMS version="0.2"><ITEM item_id="5"/></ALLOWEDITEMS></MAP></GAMESTYLE>
<UNKNOWNELEMENT/>
<GAMESTYLE type="deathmatch"><MAP name="NotRead" dir="x"/></GAMESTYLE>
</MAP_LIST>
"""

DEFAULT_RULES = [
    [32, 20, 0, 3, 10, 5, 1, 0, 0, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1],
    [32, 0, 30, 3, 10, 30, 1, 1, 1, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1],
    [32, 0, 5, 3, 10, 0, 0, 1, 1, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1],
    [6, 0, 0, 3, 10, 5, 1, 1, 0, 0, 5, 100, 1, 1, 1, 1, 0, 3, 30, 1, 0, 1],
]


def chunk(cid, payload):
    return struct.pack("<HI", cid, len(payload) + 6) + payload


def ints(values):
    return b"".join(struct.pack("<i", v) for v in values)


def names(*ns):
    return b"".join(chunk(0x4D2, n.encode() + b"\0") for n in ns)


def main():
    out = os.path.join(sys.argv[1], "edge")
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "mpmaplist.txt"), "w", newline="\n").write(LIST)

    # edge.sav: the older rule chunks, each later one shorter (so each leaves some of its values), the old
    # options, a chunk not known, rotations, the name, password and port, then a broken rotation.
    rules = [list(r) for r in DEFAULT_RULES]
    parts = []
    # 0x1B4: four games' first 17, the last game's cut short (the read fails, nothing kept).
    data = b"".join(ints([400 + 22 * g + i for i in range(17)]) for g in range(3)) + ints([9] * 5)
    parts.append(chunk(0x1B4, data))
    for g in range(3):
        rules[g][:17] = [400 + 22 * g + i for i in range(17)]
    # 0x1AE: three games' first 14.
    parts.append(chunk(0x1AE, b"".join(ints([200 + 22 * g + i for i in range(14)]) for g in range(4))))
    for g in range(3):
        rules[g][:14] = [200 + 22 * g + i for i in range(14)]
    # 0x1AA: three games' first 13.
    parts.append(chunk(0x1AA, b"".join(ints([100 + 22 * g + i for i in range(13)]) for g in range(4))))
    for g in range(3):
        rules[g][:13] = [100 + 22 * g + i for i in range(13)]
    # 0x1AD: +0x18, +0x10, +0x14, +0x15 (the +0x6C zeroed: 0x1B7 sets it first).
    parts.append(chunk(0x1B7, ints([5, 6, 7]) + bytes([8, 9])))
    parts.append(chunk(0x1AD, ints([41, 42]) + bytes([43, 44])))
    # A chunk not known, skipped whole.
    parts.append(chunk(0x1B3, ints([1, 2, 3])))
    # Rotations: 2 (all four lists, names known and not), 7 (out of range: skipped), 0 (a list not known),
    # 3 twice (the second load empties the first's).
    rot2 = (chunk(0x1A4, names("Dm1", "Nobody")) + chunk(0x1A6, names("team1", "Team2")) +
            chunk(0x1A8, names("Obj1")) + chunk(0x1B2, names("Coop1", " UNKNOWN ")))
    parts.append(chunk(0x1B6, chunk(0x1B5, ints([2])) + rot2))
    parts.append(chunk(0x1B6, chunk(0x1B5, ints([7])) + chunk(0x1A4, names("Dm1"))))
    parts.append(chunk(0x1B6, chunk(0x1B5, ints([5])) + chunk(0x1A4, names("Dm1"))))
    parts.append(chunk(0x1B6, chunk(0x1B5, ints([0])) + chunk(0x1A5, names("Dm1")) + chunk(0x1A4, names("Dm1"))))
    parts.append(chunk(0x1B6, chunk(0x1B5, ints([3])) + chunk(0x1A6, names("Team1")) + chunk(0x1A8, names("Obj1"))))
    parts.append(chunk(0x1B6, chunk(0x1B5, ints([3])) + chunk(0x1A4, names("Dm2"))))
    parts.append(chunk(0x1AB, b"Edge server\0"))
    parts.append(chunk(0x1AC, b"secret\0"))
    parts.append(chunk(0x1AF, ints([12345])))
    # A rotation chunk whose first child is not its index: nothing read, and the load ends inside it (only
    # the child is left), so the port after it is not read.
    parts.append(chunk(0x1B6, chunk(0x1A4, names("Dm1"))))
    parts.append(chunk(0x1AF, ints([999])))
    root = chunk(0x4E6, chunk(0x44D, b"".join(parts)) + chunk(0x4B0, b""))
    open(os.path.join(out, "edge.sav"), "wb").write(root)

    # edge2.sav: 0x1B0, then 0x1B8 cut short after two games; the options; a rotation; no name.
    rules2 = [list(r) for r in DEFAULT_RULES]
    parts = [chunk(0x1AF, ints([2222])),
             chunk(0x1B0, b"".join(ints([500 + 22 * g + i for i in range(17)]) for g in range(4)))]
    for g in range(3):
        rules2[g][:17] = [500 + 22 * g + i for i in range(17)]
    parts.append(chunk(0x1B8, b"".join(ints([600 + 22 * g + i for i in range(22)]) for g in range(2)) + ints([1] * 10)))
    for g in range(2):
        rules2[g] = [600 + 22 * g + i for i in range(22)]
    parts.append(chunk(0x1B7, ints([71, 72, 73]) + bytes([74, 75])))
    parts.append(chunk(0x1B6, chunk(0x1B5, ints([4])) + chunk(0x1B2, names("coop1")) + chunk(0x1A4, names("dm2", "Dm1"))))
    root = chunk(0x4E6, chunk(0x44D, b"".join(parts)))
    open(os.path.join(out, "edge2.sav"), "wb").write(root)

    maps = ["type 0: 3 maps",
             "map 'Dm1' 'Dm1_dir' 0 -1 -1 -1 -1 0 -1", "  presets ''",
             "map 'dm1' 'Dm1_second' 0 -1 -1 -1 -1 0 -1", "  presets ''",
             "map 'Dm2' 'Dm2_dir' 0 -1 -1 -1 -1 0 -1", "  presets ''",
             "type 1: 2 maps",
             "map 'Team1' 'Team1_dir' 0 -1 -1 -1 -1 1 -1", "  presets ''",
             "map 'Team2' 'Team2_dir' 0 -1 -1 -1 -1 0 -1", "  item 7 -1 8", "  item -1 3 -1", "  presets ''",
             "type 2: 1 maps",
             "map 'Obj1' 'Obj1_dir' 0 -1 -1 -1 -1 0 -1", "  presets ''",
             "type 3: 2 maps",
             "map 'Coop1' 'Coop1_dir' 0 12 -1 14 13 0 77",
             "  objective 2 1 3 1 0 9", "  objective -1 4 5 -1 1 -1", "  objective 6 -1 -1 -1 -1 -1",
             "  objective -1 -1 -1 -1 1 -1",
             "  item 1 -1 -1", "  item 22 -1 -1", "  item 333 -1 -1", "  item 4 -1 -1", "  item 4 -1 -1",
             "  presets '\"A preset\"'",
             "map ' UNKNOWN ' ' UNKNOWN ' 0 -1 21 -1 -1 0 -1", "  presets ''"]

    # The maps the rotations find (FindMap searches the last first: "Dm1" is the second, "dm1").
    shown = {"Dm1": "'dm1' (Dm1_second)", "Dm2": "'Dm2' (Dm2_dir)", "Team1": "'Team1' (Team1_dir)",
             "Team2": "'Team2' (Team2_dir)", "Obj1": "'Obj1' (Obj1_dir)", "Coop1": "'Coop1' (Coop1_dir)",
             " UNKNOWN ": "' UNKNOWN ' ( UNKNOWN )"}

    def dump(header, rules, rot):
        lines = [header] + maps
        for g in range(4):
            lines.append("rules %d: %s" % (g, " ".join(str(v) for v in rules[g])))
        for r in range(5):
            for t in range(4):
                lines.append("rotation %d type %d:%s" % (r, t, "".join(" " + shown[n] for n in rot.get(r, [[]] * 4)[t])))
        return lines

    # After the load, the list read again: the defaults, no rotation (+0x6C, the options' last, is kept).
    def expected(name, header, rules, rot):
        reset = "loaded 1 name 'H&D 2 SERVER' password '' port 11001 options 0 0 0 0 " + header.split()[-1]
        lines = dump(header, rules, rot) + ["reset"] + dump(reset, DEFAULT_RULES, {})
        open(os.path.join(out, name), "w", newline="\n").write("\n".join(lines) + "\n")

    expected("edge.txt", "loaded 1 name 'Edge server' password 'secret' port 12345 options 42 43 44 41 0", rules,
             {0: [["Dm1"], [], [], []], 2: [["Dm1"], ["Team1", "Team2"], ["Obj1"], ["Coop1", " UNKNOWN "]],
              3: [["Dm2"], [], [], []]})
    expected("edge2.txt", "loaded 1 name 'H&D 2 SERVER' password '' port 2222 options 73 74 75 72 71", rules2,
             {4: [["Dm2", "Dm1"], [], [], ["Coop1"]]})

    # The filter (part 9h-ii): files of a root 0x4E6 holding only 0x4B0, and the filter line each gives.
    def filt(ping, gtype, flags, u10=0, u11=0):
        return ints([ping, gtype]) + bytes(flags), bytes([u10, u11])
    all_a, _ = filt(80, 3, [1, 0, 1, 0, 1, 1, 0])
    old, _ = filt(50, 2, [1, 1, 0, 1, 0, 1])                # +0x08..+0x0C, then +0x0E
    cases = [
        # 0x4B3 (sameVersion set), then the older 0x4B1 (clears it), 0x4B2, a chunk not known.
        (chunk(0x4B0, chunk(0x514, ints([0x42])) + chunk(0x4B3, all_a) + chunk(0x4B1, old) +
               chunk(0x4B2, bytes([5, 6])) + chunk(0x4B9, ints([1]))),
         "filter 50 2 1 1 0 1 0 0 1 5 6"),
        (chunk(0x4B0, chunk(0x514, ints([0x42])) + chunk(0x4B3, all_a)), "filter 80 3 1 0 1 0 1 1 0 0 0"),
        # Another version, no version first, another inner chunk: nothing read.
        (chunk(0x4B0, chunk(0x514, ints([0x41])) + chunk(0x4B3, all_a)), "filter -1 -1 0 0 0 0 0 0 0 0 0"),
        (chunk(0x4B0, chunk(0x4B3, all_a)), "filter -1 -1 0 0 0 0 0 0 0 0 0"),
        (chunk(0x4B1, chunk(0x514, ints([0x42])) + chunk(0x4B3, all_a)), "filter -1 -1 0 0 0 0 0 0 0 0 0"),
    ]
    # The set-up after the filter: read (the filter's load leaves its inner chunk).
    after = chunk(0x44D, chunk(0x1AF, ints([4321])))
    cases.append((chunk(0x4B0, chunk(0x514, ints([0x42])) + chunk(0x4B3, all_a)),
                  "filter 80 3 1 0 1 0 1 1 0 0 0"))
    for k, (inner, line) in enumerate(cases):
        os.makedirs(os.path.join(out, "filter%d" % k), exist_ok=True)
        tail = after if k == len(cases) - 1 else b""
        open(os.path.join(out, "filter%d" % k, "MPSetings.sav"), "wb").write(chunk(0x4E6, chunk(0x4B0, inner) + tail))
    open(os.path.join(out, "filters.txt"), "w", newline="\n").write("\n".join(line for _, line in cases) + "\n")

    # The elements that end the list: each list has a map, the element, then a map not read.
    terms = ['<GAMESTYLE type="Deathmatch">', "<GAMESTYLE>", '<BALANCED allied="1"/>', '<BALANCED axis="1"/>',
             "<OTHER/>", '<ITEM item_id="1"/>']
    for k, term in enumerate(terms):
        text = ('<MAP_LIST><GAMESTYLE type="deathmatch"><MAP name="A" dir="a"/>%s<MAP name="B" dir="b"/>'
                "</GAMESTYLE></MAP_LIST>" % term.replace("/>", "/>") if not term.startswith("<GAMESTYLE") else
                '<MAP_LIST><GAMESTYLE type="deathmatch"><MAP name="A" dir="a"/></GAMESTYLE>%s<MAP name="B" dir="b"/>'
                "</GAMESTYLE></MAP_LIST>" % term)
        open(os.path.join(out, "term%d.txt" % k), "w", newline="\n").write(text)
    # A MAP before any GAMESTYLE ends it too (nothing read).
    open(os.path.join(out, "term%d.txt" % len(terms)), "w", newline="\n").write(
        '<MAP_LIST><MAP name="A" dir="a"/><GAMESTYLE type="deathmatch"><MAP name="B" dir="b"/></GAMESTYLE></MAP_LIST>')
    open(os.path.join(out, "terms.txt"), "w", newline="\n").write(
        "\n".join("type 0: %d maps" % n for n in [1] * len(terms) + [0]) + "\n")


if __name__ == "__main__":
    main()
