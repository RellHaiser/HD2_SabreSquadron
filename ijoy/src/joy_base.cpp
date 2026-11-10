// CreateJoyBase, the JoyBase class (device list) and the leak report.
#include "ijoy_internal.h"

#include <cstring>

LeakCounters g_leaks;

// 0x10005d60
LeakCounters::LeakCounters()
{
    joyBaseRefs = joyRefs = effectRefs = 0;
    joyBases = joys = effects = 0;
    report = false;
}

// 0x10005d80 (run from the CRT's atexit list at 0x10001030)
LeakCounters::~LeakCounters()
{
    if (!report)
        return;
    char text[512];
    char line[128];
    text[0] = '\0';
    const struct {
        int count;
        const char *format;
    } lines[] = {
        {joyBaseRefs, "Not released:  %d  reference(s) of JoyBase.\n"},
        {joyBases, "Not released:  %d  JoyBase(s).\n"},
        {joyRefs, "Not released:  %d  reference(s) of Joy.\n"},
        {joys, "Not released:  %d  Joy(s).\n"},
        {effectRefs, "Not released:  %d  reference(s) of Effect.\n"},
        {effects, "Not released:  %d  Effect(s).\n"},
    };
    for (const auto &l : lines) {
        if (l.count != 0) {
            wsprintfA(line, l.format, l.count);
            lstrcatA(text, line);
        }
    }
    if (lstrlenA(text) != 0) {
        MessageBeep(0xFFFFFFFF);
        MessageBoxA(nullptr, text, "JoyBase error message", MB_OK);
    }
}

// 0x10001040
IJOY_API bool __stdcall CreateJoyBase(IJoyBase **joyBase, HINSTANCE hinstance, HWND hwnd,
                                      IDirectInput7A *directInput, DWORD flags)
{
    *joyBase = nullptr;
    if (!hinstance || !hwnd)
        return false;
    if (flags & IJOY_REPORT_LEAKS)
        g_leaks.report = true;
    bool ok = false;
    JoyBase *base = new JoyBase(hinstance, hwnd, directInput, &ok);
    *joyBase = base;
    if (ok)
        return true;
    delete base;
    *joyBase = nullptr;
    return false;
}

namespace {

// DIEnumDevicesCallback (0x10005930): appends the joystick to the list with its own device.
BOOL CALLBACK CollectJoystick(const DIDEVICEINSTANCEA *instance, void *context)
{
    DeviceList *list = static_cast<DeviceList *>(context);
    list->ok = false;
    DeviceEntry *entry = new DeviceEntry;
    std::memset(entry, 0, sizeof(*entry));
    entry->instance = new DIDEVICEINSTANCEA(*instance);
    IDirectInputDevice7A *device;
    if (FAILED(list->directInput->CreateDeviceEx(instance->guidInstance, IID_IDirectInputDevice7A,
                                                 reinterpret_cast<void **>(&device), nullptr))) {
        // The entry leaks and the enumeration stops, as in the original.
        OutputDebugStringA("Failed to create device.\n");
        return DIENUM_STOP;
    }
    entry->device = device;
    if (!device)
        return DIENUM_STOP;
    entry->next = nullptr;
    if (!list->devices) {
        list->devices = entry;
    } else {
        DeviceEntry *last = list->devices;
        while (last->next)
            last = last->next;
        last->next = entry;
    }
    list->ok = true;
    return DIENUM_CONTINUE;
}

// DIEnumDeviceObjectsCallback (0x10005a00): appends one object to the device entry.
BOOL CALLBACK CollectDeviceObject(const DIDEVICEOBJECTINSTANCEA *info, void *context)
{
    DeviceEntry *entry = static_cast<DeviceEntry *>(context);
    entry->objectsOk = false;
    DeviceObject *object = new DeviceObject();
    object->info = new DIDEVICEOBJECTINSTANCEA(*info);
    object->next = nullptr;
    if (!entry->objects) {
        entry->objects = object;
    } else {
        DeviceObject *last = entry->objects;
        while (last->next)
            last = last->next;
        last->next = object;
    }
    entry->objectsOk = true;
    return DIENUM_CONTINUE;
}

}  // namespace

// 0x100011f0
JoyBase::JoyBase(HINSTANCE hinstance, HWND hwnd, IDirectInput7A *directInput, bool *ok)
{
    refCount_ = 0;
    *ok = false;
    hinstance_ = hinstance;
    hwnd_ = hwnd;
    dinput_ = nullptr;
    numJoys_ = 0;
    openJoys_ = nullptr;
    std::memset(&list_, 0, sizeof(list_));
    if (!directInput) {
        if (FAILED(DirectInputCreateEx(hinstance_, DIRECTINPUT_VERSION, IID_IDirectInput7A,
                                       reinterpret_cast<void **>(&dinput_), nullptr)))
            return;
    } else {
        dinput_ = directInput;
        directInput->AddRef();
    }
    if (!EnumerateDevices()) {
        FreeDevices();
        return;
    }
    ++g_leaks.joyBases;
    AddRef();
    *ok = true;
}

// 0x100010d0. A JoyBase whose constructor failed is still counted down here.
JoyBase::~JoyBase()
{
    FreeDevices();
    --g_leaks.joyBases;
}

// 0x100012f0
int JoyBase::AddRef()
{
    ++g_leaks.joyBaseRefs;
    return ++refCount_;
}

// 0x100012c0
int JoyBase::Release()
{
    int count = --refCount_;
    --g_leaks.joyBaseRefs;
    if (refCount_ == 0)
        delete this;
    return count;
}

// 0x10001310: collects the attached joysticks with their objects and capabilities.
bool JoyBase::EnumerateDevices()
{
    list_.directInput = dinput_;
    list_.hwnd = hwnd_;
    list_.ok = false;
    list_.devices = nullptr;
    dinput_->EnumDevices(DIDEVTYPE_JOYSTICK, CollectJoystick, &list_, DIEDFL_ATTACHEDONLY);
    if (!list_.ok)
        return false;
    for (DeviceEntry *entry = list_.devices; entry; entry = entry->next) {
        entry->device->EnumObjects(CollectDeviceObject, entry, DIDFT_ALL);
        // The original tests the device-list flag here, not the entry's objectsOk.
        if (!list_.ok)
            return false;
        std::memset(&entry->caps, 0, sizeof(entry->caps));
        entry->caps.dwSize = sizeof(DIDEVCAPS);
        entry->device->GetCapabilities(&entry->caps);
        entry->device->Release();
        entry->device = nullptr;
        ++numJoys_;
    }
    return true;
}

// 0x10001150
void JoyBase::FreeDevices()
{
    while (DeviceEntry *entry = list_.devices) {
        list_.devices = entry->next;
        while (DeviceObject *object = entry->objects) {
            entry->objects = object->next;
            delete object->info;
            delete object;
        }
        if (entry->device)
            entry->device->Release();
        delete entry->instance;
        delete entry;
    }
    while (OpenJoy *open = openJoys_) {
        openJoys_ = open->next;
        delete open;
    }
    if (dinput_) {
        dinput_->Release();
        dinput_ = nullptr;
    }
}

DeviceEntry *JoyBase::Device(int index)
{
    DeviceEntry *entry = list_.devices;
    for (; index > 0; --index)
        entry = entry->next;
    return entry;
}

namespace {

// The object at data-format offset `offset`, of type `type` if given.
const DIDEVICEOBJECTINSTANCEA *FindObject(const DeviceEntry *entry, DWORD offset, const GUID *type)
{
    for (const DeviceObject *object = entry->objects; object; object = object->next) {
        if ((!type || object->info->guidType == *type) && object->info->dwOfs == offset)
            return object->info;
    }
    return nullptr;
}

}  // namespace

// The axis/button/POV lookups shared with Joy (which passes its own device entry).
const char *AxisName(const DeviceEntry *entry, int axis)
{
    if (axis < 0 || axis >= IJOY_NUM_AXES)
        return nullptr;
    const DIDEVICEOBJECTINSTANCEA *info = FindObject(entry, AxisOffset(axis), nullptr);
    return info ? info->tszName : nullptr;
}

const char *ButtonName(const DeviceEntry *entry, int button)
{
    if (button < 0 || button >= static_cast<int>(entry->caps.dwButtons))
        return nullptr;
    DWORD offset = (FIELD_OFFSET(DIJOYSTATE2, rgbButtons) + button) & 0xFFFF;
    const DIDEVICEOBJECTINSTANCEA *info = FindObject(entry, offset, &GUID_Button);
    return info ? info->tszName : nullptr;
}

const char *PovName(const DeviceEntry *entry, int pov)
{
    if (pov < 0 || pov >= IJOY_NUM_POVS)
        return nullptr;
    DWORD offset = FIELD_OFFSET(DIJOYSTATE2, rgdwPOV) + pov * sizeof(DWORD);
    const DIDEVICEOBJECTINSTANCEA *info = FindObject(entry, offset, &GUID_POV);
    return info ? info->tszName : nullptr;
}

// Fills `info`; false (with `info` zeroed) when the axis has no object.
bool AxisInfo(const DeviceEntry *entry, int axis, IJoyAxisInfo *info)
{
    const DIDEVICEOBJECTINSTANCEA *object = FindObject(entry, AxisOffset(axis), nullptr);
    if (!object) {
        std::memset(info, 0, sizeof(*info));
        return false;
    }
    info->name = object->tszName;
    info->ffActuator = (object->dwFlags & DIDOI_FFACTUATOR) != 0;
    info->ffForceResolution = object->dwFFForceResolution;
    info->ffMaxForce = object->dwFFMaxForce;
    // Checked one after the other on the converted value, as in the original.
    DWORD aspect = object->dwFlags & DIDOI_ASPECTMASK;
    info->aspect = static_cast<int>(aspect);
    if (aspect == DIDOI_ASPECTACCEL)
        info->aspect = IJOY_ASPECT_ACCELERATION;
    if (info->aspect == DIDOI_ASPECTFORCE)
        info->aspect = IJOY_ASPECT_FORCE;
    if (info->aspect == DIDOI_ASPECTPOSITION)
        info->aspect = IJOY_ASPECT_POSITION;
    if (info->aspect == DIDOI_ASPECTVELOCITY)
        info->aspect = IJOY_ASPECT_VELOCITY;
    return true;
}

void JoyInfo(const DeviceEntry *entry, bool forceFeedback, IJoyInfo *info)
{
    info->instanceName = entry->instance->tszInstanceName;
    info->productName = entry->instance->tszProductName;
    info->numAxes = entry->caps.dwAxes;
    info->numButtons = entry->caps.dwButtons;
    info->numPovs = entry->caps.dwPOVs;
    info->forceFeedback = forceFeedback;
    BYTE subType = GET_DIDEVICE_SUBTYPE(entry->instance->dwDevType);
    info->subType = subType >= 1 && subType <= 7 ? subType : 0;
}

// 0x10001550. No check against the number of joysticks beyond the count being nonzero.
const char *JoyBase::GetAxisName(int joy, int axis)
{
    if (numJoys_ == 0 || joy < 0 || joy >= numJoys_)
        return nullptr;
    return AxisName(Device(joy), axis);
}

// 0x10001620
bool JoyBase::GetAxisInfo(int joy, int axis, IJoyAxisInfo *info)
{
    std::memset(info, 0, sizeof(*info));
    if (numJoys_ == 0 || joy < 0 || joy >= numJoys_ || !info || axis < 0 || axis >= IJOY_NUM_AXES)
        return false;
    return ::AxisInfo(Device(joy), axis, info);
}

// 0x100014d0
const char *JoyBase::GetButtonName(int joy, int button)
{
    if (numJoys_ == 0 || joy < 0 || joy >= numJoys_)
        return nullptr;
    return ButtonName(Device(joy), button);
}

// 0x100014c0
int JoyBase::GetNumJoys()
{
    return numJoys_;
}

// 0x10001880
IDirectInput7A *JoyBase::GetDirectInput()
{
    return dinput_;
}

// 0x100013c0
bool JoyBase::GetJoyInfo(IJoyInfo *info, int joy)
{
    if (joy < 0 || joy >= numJoys_ || !info)
        return false;
    const DeviceEntry *entry = Device(joy);
    JoyInfo(entry, (entry->caps.dwFlags & DIDC_FORCEFEEDBACK) != 0, info);
    return true;
}

// 0x100017d0
const char *JoyBase::GetPovName(int joy, int pov)
{
    if (numJoys_ == 0 || joy < 0 || joy >= numJoys_)
        return nullptr;
    return PovName(Device(joy), pov);
}

// 0x10001890
bool JoyBase::RunControlPanel()
{
    return SUCCEEDED(dinput_->RunControlPanel(list_.hwnd, 0));
}

// 0x100018b0
bool JoyBase::CreateJoy(IJoy **joy, int index, DWORD flags)
{
    *joy = nullptr;
    if (numJoys_ == 0 || index < 0 || index >= numJoys_)
        return false;
    DeviceEntry *entry = Device(index);
    for (OpenJoy *open = openJoys_; open; open = open->next) {
        if (open->index == index) {
            *joy = open->joy;
            open->joy->AddRef();
            return true;
        }
    }
    OpenJoy *open = new OpenJoy;
    open->index = index;
    open->next = nullptr;
    bool ok = false;
    open->joy = new Joy(this, entry, &list_, flags, &ok);
    if (!ok) {
        delete open->joy;
        delete open;
        return false;
    }
    open->next = openJoys_;
    openJoys_ = open;
    *joy = open->joy;
    return true;
}
