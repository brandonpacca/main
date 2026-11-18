// MadCatz Wireless Force Feedback Racing Wheel (Xbox 360) -> vJoy bridge per Windows 11
//
// Il volante, collegato tramite il "Xbox 360 Wireless Receiver for Windows", viene gestito
// dal driver Microsoft xusb22 ed e' visibile solo come dispositivo XInput (sottotipo WHEEL).
// In DirectInput Windows lo espone con i pedali combinati su un solo asse e senza force
// feedback, cosa che rende il volante quasi inutilizzabile nei giochi di guida.
//
// Questo programma:
//   * legge il volante via XInput (incluso il tasto Guide tramite XInputGetStateEx)
//   * lo riespone come periferica DirectInput virtuale vJoy con assi separati
//     (sterzo / acceleratore / freno), 15 pulsanti e POV (croce direzionale)
//   * riceve gli effetti Force Feedback che il gioco invia a vJoy (constant, ramp,
//     periodic, spring, damper, friction, inertia), li somma e li traduce nei due
//     motori XInput del volante
//
// Uso:  madcatz_wheel.exe            avvio normale
//       madcatz_wheel.exe --list     elenca i controller XInput collegati
//       madcatz_wheel.exe --monitor  mostra i valori grezzi (per verificare la mappatura)
//       madcatz_wheel.exe --ffb-test prova i motori (per calibrare la direzione del FFB)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include "vjoy_api.h"

// =====================================================================================
//  XInput (caricato dinamicamente)
// =====================================================================================
struct XGamepad {
    WORD  wButtons;
    BYTE  bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct XState { DWORD dwPacketNumber; XGamepad Gamepad; DWORD pad[4]; }; // pad: margine per GetStateEx
struct XVibration { WORD wLeftMotorSpeed, wRightMotorSpeed; };
struct XCaps { BYTE Type, SubType; WORD Flags; XGamepad Gamepad; XVibration Vibration; };

enum : WORD {
    XB_DPAD_UP = 0x0001, XB_DPAD_DOWN = 0x0002, XB_DPAD_LEFT = 0x0004, XB_DPAD_RIGHT = 0x0008,
    XB_START = 0x0010, XB_BACK = 0x0020, XB_LTHUMB = 0x0040, XB_RTHUMB = 0x0080,
    XB_LB = 0x0100, XB_RB = 0x0200, XB_GUIDE = 0x0400,
    XB_A = 0x1000, XB_B = 0x2000, XB_X = 0x4000, XB_Y = 0x8000,
};
static const BYTE XSUBTYPE_WHEEL = 0x02;

struct XInputApi {
    HMODULE dll = nullptr;
    DWORD (WINAPI* GetState)(DWORD, XState*) = nullptr;
    DWORD (WINAPI* GetStateEx)(DWORD, XState*) = nullptr;  // ordinale 100, legge anche Guide
    DWORD (WINAPI* SetState)(DWORD, XVibration*) = nullptr;
    DWORD (WINAPI* GetCaps)(DWORD, DWORD, XCaps*) = nullptr;

    bool load() {
        const wchar_t* names[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
        for (auto n : names) { if ((dll = LoadLibraryW(n))) break; }
        if (!dll) return false;
        GetState   = reinterpret_cast<decltype(GetState)>(reinterpret_cast<void*>(GetProcAddress(dll, "XInputGetState")));
        SetState   = reinterpret_cast<decltype(SetState)>(reinterpret_cast<void*>(GetProcAddress(dll, "XInputSetState")));
        GetCaps    = reinterpret_cast<decltype(GetCaps)>(reinterpret_cast<void*>(GetProcAddress(dll, "XInputGetCapabilities")));
        GetStateEx = reinterpret_cast<decltype(GetStateEx)>(reinterpret_cast<void*>(GetProcAddress(dll, MAKEINTRESOURCEA(100))));
        return GetState && SetState && GetCaps;
    }
    DWORD read(DWORD slot, XState* s) const {
        ZeroMemory(s, sizeof(*s));
        return GetStateEx ? GetStateEx(slot, s) : GetState(slot, s);
    }
} g_xi;

// =====================================================================================
//  Configurazione (madcatz_wheel.ini accanto all'eseguibile)
// =====================================================================================
struct ButtonDef { const wchar_t* name; WORD mask; int defVjoy; };
static const ButtonDef kButtons[] = {
    { L"A", XB_A, 1 },        { L"B", XB_B, 2 },          { L"X", XB_X, 3 },
    { L"Y", XB_Y, 4 },        { L"LB", XB_LB, 5 },        { L"RB", XB_RB, 6 },
    { L"BACK", XB_BACK, 7 },  { L"START", XB_START, 8 },  { L"LS", XB_LTHUMB, 9 },
    { L"RS", XB_RTHUMB, 10 }, { L"GUIDE", XB_GUIDE, 11 }, { L"DPAD_UP", XB_DPAD_UP, 12 },
    { L"DPAD_DOWN", XB_DPAD_DOWN, 13 }, { L"DPAD_LEFT", XB_DPAD_LEFT, 14 },
    { L"DPAD_RIGHT", XB_DPAD_RIGHT, 15 },
};
static const int kNumButtons = sizeof(kButtons) / sizeof(kButtons[0]);

struct Config {
    // generale
    int  vjoyId = 1;
    int  slot = -1;          // -1 = auto (primo volante trovato)
    int  pollMs = 2;
    bool status = true;
    // sterzo
    UINT  steerAxis = vj::HID_X;
    bool  steerInvert = false;
    double steerDeadzone = 0.0, steerSaturation = 1.0, steerGamma = 1.0, steerCenter = 0.0;
    // pedali
    bool  combined = false;
    UINT  accAxis = vj::HID_Y, brakeAxis = vj::HID_Z;
    bool  accInvert = false, brakeInvert = false;
    double pedalDzLow = 0.0, pedalDzHigh = 0.0, accGamma = 1.0, brakeGamma = 1.0;
    // assi extra
    UINT lyAxis = vj::HID_RX, rxAxis = vj::HID_RY, ryAxis = vj::HID_RZ;
    // pulsanti
    int  btnMap[kNumButtons] = {};
    bool pov = true;
    // force feedback
    bool   ffb = true;
    int    ffbMode = 0;       // 0 = directional, 1 = rumble, 2 = left, 3 = right
    bool   ffbInvert = false;
    double ffbGain = 1.0, ffbMinForce = 0.0, ffbDamperScale = 0.25, ffbInertiaScale = 0.02;
    int    ffbRateMs = 8;
} g_cfg;

static std::wstring g_iniPath, g_exeDir;

static std::wstring iniStr(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[128];
    GetPrivateProfileStringW(sec, key, def, buf, 128, g_iniPath.c_str());
    // rimuove commenti in linea e spazi
    std::wstring s(buf);
    size_t c = s.find_first_of(L";#");
    if (c != std::wstring::npos) s.resize(c);
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    while (!s.empty() && iswspace(s.front())) s.erase(0, 1);
    return s;
}
static int iniInt(const wchar_t* sec, const wchar_t* key, int def) {
    std::wstring s = iniStr(sec, key, L"");
    return s.empty() ? def : _wtoi(s.c_str());
}
static double iniDbl(const wchar_t* sec, const wchar_t* key, double def) {
    std::wstring s = iniStr(sec, key, L"");
    if (s.empty()) return def;
    for (auto& ch : s) if (ch == L',') ch = L'.';
    return wcstod(s.c_str(), nullptr);
}
static bool iniBool(const wchar_t* sec, const wchar_t* key, bool def) {
    return iniInt(sec, key, def ? 1 : 0) != 0;
}
static UINT parseAxis(const std::wstring& s, UINT def) {
    struct { const wchar_t* n; UINT a; } t[] = {
        { L"X", vj::HID_X }, { L"Y", vj::HID_Y }, { L"Z", vj::HID_Z }, { L"RX", vj::HID_RX },
        { L"RY", vj::HID_RY }, { L"RZ", vj::HID_RZ }, { L"SL0", vj::HID_SL0 }, { L"SL1", vj::HID_SL1 },
        { L"NONE", 0 },
    };
    if (s.empty()) return def;
    for (auto& e : t) if (_wcsicmp(s.c_str(), e.n) == 0) return e.a;
    return def;
}
static UINT iniAxis(const wchar_t* sec, const wchar_t* key, UINT def) {
    return parseAxis(iniStr(sec, key, L""), def);
}
static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void loadConfig() {
    Config& c = g_cfg;
    c.vjoyId = iniInt(L"general", L"vjoy_device", 1);
    c.slot   = iniInt(L"general", L"xinput_slot", -1);
    c.pollMs = (int)clampd(iniInt(L"general", L"poll_ms", 2), 1, 50);
    c.status = iniBool(L"general", L"show_status", true);

    c.steerAxis       = iniAxis(L"steering", L"axis", vj::HID_X);
    c.steerInvert     = iniBool(L"steering", L"invert", false);
    c.steerDeadzone   = clampd(iniDbl(L"steering", L"deadzone", 0.0) / 100.0, 0.0, 0.5);
    c.steerSaturation = clampd(iniDbl(L"steering", L"saturation", 100.0) / 100.0, 0.1, 1.0);
    c.steerGamma      = clampd(iniDbl(L"steering", L"linearity", 1.0), 0.2, 5.0);
    c.steerCenter     = clampd(iniDbl(L"steering", L"center_offset", 0.0) / 100.0, -0.5, 0.5);

    c.combined    = _wcsicmp(iniStr(L"pedals", L"mode", L"separate").c_str(), L"combined") == 0;
    c.accAxis     = iniAxis(L"pedals", L"accelerator_axis", vj::HID_Y);
    c.brakeAxis   = iniAxis(L"pedals", L"brake_axis", vj::HID_Z);
    c.accInvert   = iniBool(L"pedals", L"accelerator_invert", false);
    c.brakeInvert = iniBool(L"pedals", L"brake_invert", false);
    c.pedalDzLow  = clampd(iniDbl(L"pedals", L"deadzone_low", 0.0) / 100.0, 0.0, 0.45);
    c.pedalDzHigh = clampd(iniDbl(L"pedals", L"deadzone_high", 0.0) / 100.0, 0.0, 0.45);
    c.accGamma    = clampd(iniDbl(L"pedals", L"accelerator_linearity", 1.0), 0.2, 5.0);
    c.brakeGamma  = clampd(iniDbl(L"pedals", L"brake_linearity", 1.0), 0.2, 5.0);

    c.lyAxis = iniAxis(L"extra_axes", L"thumb_ly", vj::HID_RX);
    c.rxAxis = iniAxis(L"extra_axes", L"thumb_rx", vj::HID_RY);
    c.ryAxis = iniAxis(L"extra_axes", L"thumb_ry", vj::HID_RZ);

    for (int i = 0; i < kNumButtons; ++i)
        c.btnMap[i] = (int)clampd(iniInt(L"buttons", kButtons[i].name, kButtons[i].defVjoy), 0, 128);
    c.pov = iniBool(L"buttons", L"dpad_as_pov", true);

    c.ffb = iniBool(L"ffb", L"enabled", true);
    std::wstring m = iniStr(L"ffb", L"mode", L"directional");
    c.ffbMode = _wcsicmp(m.c_str(), L"rumble") == 0 ? 1 :
                _wcsicmp(m.c_str(), L"left") == 0   ? 2 :
                _wcsicmp(m.c_str(), L"right") == 0  ? 3 : 0;
    c.ffbInvert       = iniBool(L"ffb", L"invert", false);
    c.ffbGain         = clampd(iniDbl(L"ffb", L"gain", 100.0) / 100.0, 0.0, 3.0);
    c.ffbMinForce     = clampd(iniDbl(L"ffb", L"min_force", 0.0) / 100.0, 0.0, 0.9);
    c.ffbDamperScale  = clampd(iniDbl(L"ffb", L"damper_scale", 0.25), 0.0, 10.0);
    c.ffbInertiaScale = clampd(iniDbl(L"ffb", L"inertia_scale", 0.02), 0.0, 10.0);
    c.ffbRateMs       = (int)clampd(iniInt(L"ffb", L"update_ms", 8), 2, 100);
}

// =====================================================================================
//  Motore Force Feedback
// =====================================================================================
static const int kMaxEffects = 41;   // vJoy usa indici 1..40

struct Effect {
    bool used = false, playing = false;
    vj::FFBEType type = vj::ET_NONE;
    WORD  duration = 0xFFFF;
    double gain = 1.0;
    double dir = 1.0;                // +1 = destra, -1 = sinistra
    LONG  constMag = 0;
    LONG  rampStart = 0, rampEnd = 0;
    DWORD perMag = 0, perPhase = 0, perPeriod = 100; LONG perOff = 0;
    bool  hasCond = false; vj::FFB_EFF_COND cond{};
    DWORD startTick = 0; int loopsLeft = 0;
};

static vj::Api          g_vj;
static Effect           g_eff[kMaxEffects];
static double           g_devGain = 1.0;
static bool             g_actuators = true, g_paused = false;
static CRITICAL_SECTION g_ffbLock;
static volatile LONG    g_ffbPackets = 0;

static Effect* effAt(int idx) { return (idx >= 1 && idx < kMaxEffects) ? &g_eff[idx] : nullptr; }

static void stopAllEffects() {
    for (auto& e : g_eff) e.playing = false;
}

static void CALLBACK ffbCallback(PVOID data, PVOID) {
    auto pkt = static_cast<const vj::FFB_DATA*>(data);
    int dev = 0;
    if (g_vj.Ffb_h_DeviceID(pkt, &dev) == ERROR_SUCCESS && dev != g_cfg.vjoyId) return;
    vj::FFBPType type;
    if (g_vj.Ffb_h_Type(pkt, &type) != ERROR_SUCCESS) return;
    InterlockedIncrement(&g_ffbPackets);

    EnterCriticalSection(&g_ffbLock);
    switch (type) {
    case vj::PT_EFFREP: {
        vj::FFB_EFF_REPORT r{};
        if (g_vj.Ffb_h_Eff_Report(pkt, &r) == ERROR_SUCCESS) {
            if (Effect* e = effAt(r.EffectBlockIndex)) {
                e->used = true;
                e->type = r.EffectType;
                e->duration = r.Duration;
                e->gain = r.Gain / 255.0;
                if (r.Polar) e->dir = (r.Direction <= 0x7F) ? 1.0 : -1.0;   // 0-180° = destra
                else         e->dir = ((signed char)r.DirX < 0) ? -1.0 : 1.0;
            }
        }
        break;
    }
    case vj::PT_CONSTREP: {
        vj::FFB_EFF_CONSTANT c{};
        if (g_vj.Ffb_h_Eff_Constant(pkt, &c) == ERROR_SUCCESS)
            if (Effect* e = effAt(c.EffectBlockIndex)) { e->used = true; e->constMag = c.Magnitude; }
        break;
    }
    case vj::PT_RAMPREP: {
        vj::FFB_EFF_RAMP r{};
        if (g_vj.Ffb_h_Eff_Ramp(pkt, &r) == ERROR_SUCCESS)
            if (Effect* e = effAt(r.EffectBlockIndex)) { e->used = true; e->rampStart = r.Start; e->rampEnd = r.End; }
        break;
    }
    case vj::PT_PRIDREP: {
        vj::FFB_EFF_PERIOD p{};
        if (g_vj.Ffb_h_Eff_Period(pkt, &p) == ERROR_SUCCESS)
            if (Effect* e = effAt(p.EffectBlockIndex)) {
                e->used = true; e->perMag = p.Magnitude; e->perOff = p.Offset;
                e->perPhase = p.Phase; e->perPeriod = p.Period ? p.Period : 1;
            }
        break;
    }
    case vj::PT_CONDREP: {
        vj::FFB_EFF_COND c{};
        if (g_vj.Ffb_h_Eff_Cond(pkt, &c) == ERROR_SUCCESS && !c.isY)   // il volante ha solo l'asse X
            if (Effect* e = effAt(c.EffectBlockIndex)) { e->used = true; e->cond = c; e->hasCond = true; }
        break;
    }
    case vj::PT_EFOPREP: {
        vj::FFB_EFF_OP op{};
        if (g_vj.Ffb_h_EffOp(pkt, &op) == ERROR_SUCCESS) {
            if (op.EffectOp == vj::EFF_SOLO) stopAllEffects();
            if (Effect* e = effAt(op.EffectBlockIndex)) {
                if (op.EffectOp == vj::EFF_STOP) e->playing = false;
                else {
                    e->used = true; e->playing = true; e->startTick = GetTickCount();
                    e->loopsLeft = (op.LoopCount == 0xFF) ? -1 : (op.LoopCount > 0 ? op.LoopCount - 1 : 0);
                }
            }
        }
        break;
    }
    case vj::PT_BLKFRREP: {
        int idx = 0;
        if (g_vj.Ffb_h_EBI(pkt, &idx) == ERROR_SUCCESS)
            if (Effect* e = effAt(idx)) *e = Effect();
        break;
    }
    case vj::PT_CTRLREP: {
        vj::FFB_CTRL ctrl;
        if (g_vj.Ffb_h_DevCtrl(pkt, &ctrl) == ERROR_SUCCESS) {
            switch (ctrl) {
            case vj::CTRL_ENACT:    g_actuators = true; break;
            case vj::CTRL_DISACT:   g_actuators = false; break;
            case vj::CTRL_STOPALL:  stopAllEffects(); break;
            case vj::CTRL_DEVRST:   for (auto& e : g_eff) e = Effect();
                                    g_paused = false; g_actuators = true; g_devGain = 1.0; break;
            case vj::CTRL_DEVPAUSE: g_paused = true; break;
            case vj::CTRL_DEVCONT:  g_paused = false; break;
            }
        }
        break;
    }
    case vj::PT_GAINREP: {
        BYTE gain = 255;
        if (g_vj.Ffb_h_DevGain(pkt, &gain) == ERROR_SUCCESS) g_devGain = gain / 255.0;
        break;
    }
    default: break;
    }
    LeaveCriticalSection(&g_ffbLock);
}

static double sgn(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }

static double conditionForce(const vj::FFB_EFF_COND& c, double metric) {
    double d = metric - c.CenterPointOffset / 10000.0;
    double db = c.DeadBand / 10000.0;
    if (std::fabs(d) <= db) return 0.0;
    d -= sgn(d) * db;
    double coeff = (d > 0 ? c.PosCoeff : c.NegCoeff) / 10000.0;
    double sat   = (d > 0 ? c.PosSatur : c.NegSatur) / 10000.0;
    if (sat <= 0.0) sat = 1.0;
    return clampd(-coeff * d, -sat, sat);   // la forza si oppone allo spostamento
}

// pos: posizione volante -1..1 (positivo = destra), vel: unita'/s, acc: unita'/s^2
// ritorna forza -1..1 (positivo = spinge a destra)
static double computeForce(double pos, double vel, double acc, DWORD now) {
    EnterCriticalSection(&g_ffbLock);
    double total = 0.0;
    if (g_actuators && !g_paused) {
        for (int i = 1; i < kMaxEffects; ++i) {
            Effect& e = g_eff[i];
            if (!e.used || !e.playing) continue;
            DWORD elapsed = now - e.startTick;
            bool finite = (e.duration != 0xFFFF && e.duration != 0);
            if (finite && elapsed >= e.duration) {
                if (e.loopsLeft != 0) { if (e.loopsLeft > 0) --e.loopsLeft; e.startTick = now; elapsed = 0; }
                else { e.playing = false; continue; }
            }
            double f = 0.0;
            switch (e.type) {
            case vj::ET_CONST: f = e.constMag / 10000.0 * e.dir; break;
            case vj::ET_RAMP: {
                double t = finite ? clampd((double)elapsed / e.duration, 0.0, 1.0) : 0.0;
                f = (e.rampStart + (e.rampEnd - e.rampStart) * t) / 10000.0 * e.dir;
                break;
            }
            case vj::ET_SQR: case vj::ET_SINE: case vj::ET_TRNGL: case vj::ET_STUP: case vj::ET_STDN: {
                double x = std::fmod((double)elapsed / e.perPeriod + e.perPhase / 36000.0, 1.0);
                double w = 0.0;
                switch (e.type) {
                case vj::ET_SINE:  w = std::sin(2.0 * 3.14159265358979 * x); break;
                case vj::ET_SQR:   w = x < 0.5 ? 1.0 : -1.0; break;
                case vj::ET_TRNGL: w = 1.0 - 4.0 * std::fabs(x - 0.5); break;
                case vj::ET_STUP:  w = 2.0 * x - 1.0; break;
                default:           w = 1.0 - 2.0 * x; break;
                }
                f = (e.perOff + (double)e.perMag * w) / 10000.0 * e.dir;
                break;
            }
            case vj::ET_SPRNG: if (e.hasCond) f = conditionForce(e.cond, pos); break;
            case vj::ET_DMPR:  if (e.hasCond) f = conditionForce(e.cond, vel * g_cfg.ffbDamperScale); break;
            case vj::ET_INRT:  if (e.hasCond) f = conditionForce(e.cond, acc * g_cfg.ffbInertiaScale); break;
            case vj::ET_FRCTN:
                if (e.hasCond && std::fabs(vel) > 0.02) {
                    double coeff = (vel > 0 ? e.cond.PosCoeff : e.cond.NegCoeff) / 10000.0;
                    f = -sgn(vel) * coeff;
                }
                break;
            default: break;
            }
            total += f * e.gain;
        }
    }
    double g = g_devGain;
    LeaveCriticalSection(&g_ffbLock);
    return clampd(total * g * g_cfg.ffbGain, -1.0, 1.0);
}

static void forceToMotors(double force, XVibration& v) {
    if (g_cfg.ffbInvert) force = -force;
    double mag = std::fabs(force);
    if (mag < 0.005) { v.wLeftMotorSpeed = v.wRightMotorSpeed = 0; return; }
    mag = g_cfg.ffbMinForce + (1.0 - g_cfg.ffbMinForce) * mag;
    WORD m = (WORD)(clampd(mag, 0.0, 1.0) * 65535.0);
    switch (g_cfg.ffbMode) {
    case 1:  v.wLeftMotorSpeed = m; v.wRightMotorSpeed = m; break;    // rumble
    case 2:  v.wLeftMotorSpeed = m; v.wRightMotorSpeed = 0; break;    // solo motore sinistro
    case 3:  v.wLeftMotorSpeed = 0; v.wRightMotorSpeed = m; break;    // solo motore destro
    default:                                                          // directional
        v.wLeftMotorSpeed  = force < 0 ? m : 0;
        v.wRightMotorSpeed = force > 0 ? m : 0;
        break;
    }
}

// =====================================================================================
//  Utilita'
// =====================================================================================
static volatile bool g_running = true;
static int g_activeSlot = -1;

static BOOL WINAPI ctrlHandler(DWORD) { g_running = false; return TRUE; }

static const char* subtypeName(BYTE s) {
    switch (s) {
    case 0x01: return "Gamepad";      case 0x02: return "Volante (Wheel)";
    case 0x03: return "Arcade stick"; case 0x04: return "Flight stick";
    case 0x05: return "Dance pad";    case 0x06: return "Chitarra";
    case 0x07: return "Chitarra alt"; case 0x08: return "Batteria";
    case 0x0B: return "Chitarra bass";case 0x13: return "Arcade pad";
    default:   return "Sconosciuto";
    }
}

static int findWheelSlot(bool verbose) {
    int firstConnected = -1;
    for (DWORD i = 0; i < 4; ++i) {
        XCaps caps{};
        if (g_xi.GetCaps(i, 0, &caps) != ERROR_SUCCESS) {
            if (verbose) printf("  Slot %lu: non collegato\n", (unsigned long)i);
            continue;
        }
        if (verbose)
            printf("  Slot %lu: collegato - sottotipo 0x%02X %s\n", (unsigned long)i, caps.SubType, subtypeName(caps.SubType));
        if (firstConnected < 0) firstConnected = (int)i;
        if (caps.SubType == XSUBTYPE_WHEEL && g_cfg.slot < 0) return (int)i;
    }
    if (g_cfg.slot >= 0 && g_cfg.slot < 4) {
        XState s;
        return g_xi.read(g_cfg.slot, &s) == ERROR_SUCCESS ? g_cfg.slot : -1;
    }
    return firstConnected;   // nessun sottotipo wheel: usa il primo controller collegato
}

static void buttonsToString(WORD b, char* out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < kNumButtons; ++i) {
        if (b & kButtons[i].mask) {
            char tmp[24];
            snprintf(tmp, sizeof(tmp), "%ls ", kButtons[i].name);
            strncat(out, tmp, n - strlen(out) - 1);
        }
    }
    if (!out[0]) strncat(out, "-", n - 1);
}

// --- elaborazione assi ---
static double processSteering(SHORT raw) {
    double x = clampd(raw / 32767.0, -1.0, 1.0) + g_cfg.steerCenter;
    if (g_cfg.steerInvert) x = -x;
    double a = std::fabs(x), s = sgn(x);
    double dz = g_cfg.steerDeadzone;
    a = a <= dz ? 0.0 : (a - dz) / (1.0 - dz);
    a = clampd(a / g_cfg.steerSaturation, 0.0, 1.0);
    a = std::pow(a, g_cfg.steerGamma);
    return s * a;                              // -1..1
}
static double processPedal(BYTE raw, bool invert, double gamma) {
    double t = raw / 255.0;
    if (invert) t = 1.0 - t;
    double span = 1.0 - g_cfg.pedalDzLow - g_cfg.pedalDzHigh;
    t = clampd((t - g_cfg.pedalDzLow) / span, 0.0, 1.0);
    return std::pow(t, gamma);                 // 0..1
}
static LONG toAxis01(double v) { return (LONG)(clampd(v, 0.0, 1.0) * 32767.0 + 0.5); }
static LONG toAxisPM(double v) { return toAxis01((v + 1.0) * 0.5); }

// =====================================================================================
//  Modalita' --list / --monitor / --ffb-test
// =====================================================================================
static int runList() {
    printf("Controller XInput:\n");
    int s = findWheelSlot(true);
    if (s >= 0) printf("\nSlot selezionato: %d\n", s);
    else printf("\nNessun controller trovato. Il volante e' acceso e associato al ricevitore?\n");
    return s >= 0 ? 0 : 1;
}

static int runMonitor() {
    int slot = findWheelSlot(true);
    if (slot < 0) { printf("Nessun controller trovato.\n"); return 1; }
    printf("\nMonitor slot %d  (Guide %s)  - Ctrl+C per uscire\n"
           "Premi ogni tasto/paddle e muovi volante e pedali per vedere come vengono letti.\n\n",
           slot, g_xi.GetStateEx ? "leggibile" : "NON leggibile");
    while (g_running) {
        XState s;
        if (g_xi.read(slot, &s) != ERROR_SUCCESS) { printf("\r  [disconnesso]%60s", ""); Sleep(200); continue; }
        char btn[160];
        buttonsToString(s.Gamepad.wButtons, btn, sizeof(btn));
        printf("\rLX(sterzo)%7d  LT(freno)%4u  RT(gas)%4u  LY%7d RX%7d RY%7d  Tasti: %-40s",
               s.Gamepad.sThumbLX, s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger,
               s.Gamepad.sThumbLY, s.Gamepad.sThumbRX, s.Gamepad.sThumbRY, btn);
        fflush(stdout);
        Sleep(30);
    }
    printf("\n");
    return 0;
}

static void vibrate(int slot, WORD l, WORD r, DWORD ms) {
    XVibration v{ l, r };
    DWORD end = GetTickCount() + ms;
    while (g_running && GetTickCount() < end) { g_xi.SetState(slot, &v); Sleep(50); }
    XVibration z{ 0, 0 };
    g_xi.SetState(slot, &z);
}

static int runFfbTest() {
    int slot = findWheelSlot(true);
    if (slot < 0) { printf("Nessun controller trovato.\n"); return 1; }
    printf("\nTest motori sullo slot %d. Tieni il volante leggermente con le mani.\n\n", slot);
    const WORD half = 32768;
    printf("1) Motore SINISTRO (left motor) al 50%% per 2 s ...\n");  vibrate(slot, half, 0, 2000); Sleep(800);
    printf("2) Motore DESTRO (right motor) al 50%% per 2 s ...\n");   vibrate(slot, 0, half, 2000); Sleep(800);
    printf("3) Entrambi al 50%% per 2 s ...\n");                     vibrate(slot, half, half, 2000); Sleep(800);
    printf("4) Rampa motore sinistro 0 -> 100%% ...\n");
    for (int i = 0; i <= 20 && g_running; ++i) { XVibration v{ (WORD)(i * 3276), 0 }; g_xi.SetState(slot, &v); Sleep(150); }
    XVibration z{ 0, 0 }; g_xi.SetState(slot, &z);
    printf("\nInterpretazione (impostare in madcatz_wheel.ini, sezione [ffb]):\n"
           " - Se al passo 1 il volante tira a SINISTRA e al passo 2 a DESTRA: mode=directional, invert=0\n"
           " - Se al passo 1 tira a DESTRA e al passo 2 a SINISTRA:             mode=directional, invert=1\n"
           " - Se reagisce solo uno dei due passi o vibra soltanto:            mode=rumble (oppure left/right)\n"
           " - Al passo 4 annota la %% a cui il motore inizia a muovere il volante: usala come min_force.\n");
    return 0;
}

// =====================================================================================
//  Modalita' principale
// =====================================================================================
static bool checkVjoyDevice(UINT id, bool& haveContPov, bool& haveDiscPov, int& nButtons) {
    UINT axes[] = { g_cfg.steerAxis, g_cfg.accAxis, g_cfg.brakeAxis, g_cfg.lyAxis, g_cfg.rxAxis, g_cfg.ryAxis };
    const char* names[] = { "sterzo", "acceleratore", "freno", "thumb_ly", "thumb_rx", "thumb_ry" };
    bool ok = true;
    for (int i = 0; i < 6; ++i) {
        if (!axes[i]) continue;
        if (i == 2 && g_cfg.combined) continue;
        if (!g_vj.GetVJDAxisExist(id, axes[i])) {
            printf("  ATTENZIONE: l'asse per '%s' (usage 0x%02X) non e' abilitato nel dispositivo vJoy %u.\n", names[i], axes[i], id);
            if (i < 3) ok = false;
        }
    }
    nButtons = g_vj.GetVJDButtonNumber(id);
    int need = 0;
    for (int i = 0; i < kNumButtons; ++i) if (g_cfg.btnMap[i] > need) need = g_cfg.btnMap[i];
    if (nButtons < need)
        printf("  ATTENZIONE: vJoy %u ha %d pulsanti, la mappatura ne usa fino a %d. Configura vJoy con almeno %d pulsanti.\n",
               id, nButtons, need, need);
    haveContPov = g_vj.GetVJDContPovNumber(id) > 0;
    haveDiscPov = g_vj.GetVJDDiscPovNumber(id) > 0;
    if (g_cfg.pov && !haveContPov && !haveDiscPov)
        printf("  ATTENZIONE: nessun POV abilitato in vJoy %u: la croce sara' disponibile solo come pulsanti.\n", id);
    return ok;
}

static void updatePov(UINT id, WORD b, bool cont, bool disc) {
    bool u = b & XB_DPAD_UP, d = b & XB_DPAD_DOWN, l = b & XB_DPAD_LEFT, r = b & XB_DPAD_RIGHT;
    if (cont) {
        DWORD v = (DWORD)-1;
        if (u && r) v = 4500;   else if (r && d) v = 13500; else if (d && l) v = 22500;
        else if (l && u) v = 31500; else if (u) v = 0; else if (r) v = 9000;
        else if (d) v = 18000;  else if (l) v = 27000;
        g_vj.SetContPov(v, id, 1);
    } else if (disc) {
        int v = -1;
        if (u) v = 0; else if (r) v = 1; else if (d) v = 2; else if (l) v = 3;
        g_vj.SetDiscPov(v, id, 1);
    }
}

static void neutralVjoy(UINT id, bool cont, bool disc) {
    g_vj.ResetVJD(id);
    if (g_cfg.steerAxis) g_vj.SetAxis(toAxisPM(0.0), id, g_cfg.steerAxis);
    if (g_cfg.combined) { if (g_cfg.accAxis) g_vj.SetAxis(toAxis01(0.5), id, g_cfg.accAxis); }
    else {
        if (g_cfg.accAxis)   g_vj.SetAxis(0, id, g_cfg.accAxis);
        if (g_cfg.brakeAxis) g_vj.SetAxis(0, id, g_cfg.brakeAxis);
    }
    if (cont) g_vj.SetContPov((DWORD)-1, id, 1);
    else if (disc) g_vj.SetDiscPov(-1, id, 1);
}

static int runBridge() {
    bool ffbApi = false;
    if (!g_vj.load(g_exeDir.c_str(), ffbApi)) {
        printf("ERRORE: vJoyInterface.dll non trovata o incompatibile.\n"
               "Installa vJoy 2.1.9.1 (vedi README) oppure copia vJoyInterface.dll (x64) accanto a questo .exe.\n");
        return 2;
    }
    if (!g_vj.vJoyEnabled()) { printf("ERRORE: il driver vJoy non e' attivo. Abilitalo da 'Configure vJoy'.\n"); return 2; }

    const UINT id = (UINT)g_cfg.vjoyId;
    switch (g_vj.GetVJDStatus(id)) {
    case vj::VJD_STAT_FREE: case vj::VJD_STAT_OWN: break;
    case vj::VJD_STAT_BUSY: printf("ERRORE: vJoy %u e' gia' in uso da un altro programma.\n", id); return 2;
    case vj::VJD_STAT_MISS: printf("ERRORE: vJoy %u non esiste. Crealo con 'Configure vJoy'.\n", id); return 2;
    default:                printf("ERRORE: stato di vJoy %u sconosciuto.\n", id); return 2;
    }
    bool cont = false, disc = false; int nBtn = 0;
    if (!checkVjoyDevice(id, cont, disc, nBtn)) { printf("Correggi la configurazione di vJoy e riavvia.\n"); return 2; }
    if (!g_vj.AcquireVJD(id)) { printf("ERRORE: impossibile acquisire vJoy %u.\n", id); return 2; }
    neutralVjoy(id, cont, disc);

    bool ffbOn = false;
    if (g_cfg.ffb) {
        if (!ffbApi) printf("  Force feedback: API FFB assenti nella vJoyInterface.dll (versione vecchia?). FFB disattivato.\n");
        else if (!g_vj.IsDeviceFfb(id)) printf("  Force feedback: non abilitato sul dispositivo vJoy %u (spunta 'Enable Effects'). FFB disattivato.\n", id);
        else { g_vj.FfbRegisterGenCB(ffbCallback, nullptr); ffbOn = true; }
    }
    const char* modeNames[] = { "directional", "rumble", "left", "right" };
    printf("vJoy %u acquisito  |  pedali: %s  |  FFB: %s%s%s\n", id, g_cfg.combined ? "combinati" : "separati",
           ffbOn ? "attivo (" : "spento", ffbOn ? modeNames[g_cfg.ffbMode] : "", ffbOn ? ")" : "");
    printf("In attesa del volante... (Ctrl+C per uscire)\n\n");

    DWORD lastPacket = 0xFFFFFFFF, lastScan = 0, lastStatus = 0, lastFfb = 0;
    WORD prevButtons = 0;
    bool connected = false;
    double prevPos = 0.0, prevVel = 0.0, curPos = 0.0, acc = 0, brk = 0, lastForce = 0;
    LARGE_INTEGER freq, prevT; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prevT);
    XVibration lastVib{ 0xFFFF, 0xFFFF };

    while (g_running) {
        DWORD now = GetTickCount();
        if (!connected) {
            if (now - lastScan >= 1000) {
                lastScan = now;
                g_activeSlot = findWheelSlot(false);
                if (g_activeSlot >= 0) {
                    XCaps caps{}; g_xi.GetCaps(g_activeSlot, 0, &caps);
                    printf("\nVolante collegato sullo slot XInput %d (%s)%s\n", g_activeSlot, subtypeName(caps.SubType),
                           caps.SubType == XSUBTYPE_WHEEL ? "" : "  <- ATTENZIONE: non e' un volante");
                    connected = true; lastPacket = 0xFFFFFFFF; prevButtons = 0xFFFF;
                    lastVib = { 0xFFFF, 0xFFFF };
                }
            }
            if (!connected) { Sleep(50); continue; }
        }

        XState s;
        if (g_xi.read(g_activeSlot, &s) != ERROR_SUCCESS) {
            printf("\nVolante disconnesso.\n");
            connected = false; neutralVjoy(id, cont, disc);
            continue;
        }
        const XGamepad& gp = s.Gamepad;

        // ---- posizione/velocita' per FFB (calcolate sempre) ----
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        double dt = (double)(t.QuadPart - prevT.QuadPart) / freq.QuadPart;
        prevT = t;
        double rawPos = clampd(gp.sThumbLX / 32767.0, -1.0, 1.0);
        if (g_cfg.steerInvert) rawPos = -rawPos;
        double vel = dt > 0 ? (rawPos - prevPos) / dt : 0.0;
        vel = prevVel * 0.8 + vel * 0.2;                  // filtro passa basso
        double accel = dt > 0 ? (vel - prevVel) / dt : 0.0;
        prevPos = rawPos; prevVel = vel;

        // ---- assi e pulsanti -> vJoy (solo se lo stato e' cambiato) ----
        if (s.dwPacketNumber != lastPacket) {
            lastPacket = s.dwPacketNumber;
            curPos = processSteering(gp.sThumbLX);
            acc = processPedal(gp.bRightTrigger, g_cfg.accInvert, g_cfg.accGamma);
            brk = processPedal(gp.bLeftTrigger, g_cfg.brakeInvert, g_cfg.brakeGamma);

            if (g_cfg.steerAxis) g_vj.SetAxis(toAxisPM(curPos), id, g_cfg.steerAxis);
            if (g_cfg.combined) {
                if (g_cfg.accAxis) g_vj.SetAxis(toAxis01(0.5 + (acc - brk) * 0.5), id, g_cfg.accAxis);
            } else {
                if (g_cfg.accAxis)   g_vj.SetAxis(toAxis01(acc), id, g_cfg.accAxis);
                if (g_cfg.brakeAxis) g_vj.SetAxis(toAxis01(brk), id, g_cfg.brakeAxis);
            }
            if (g_cfg.lyAxis) g_vj.SetAxis(toAxisPM(gp.sThumbLY / 32767.0), id, g_cfg.lyAxis);
            if (g_cfg.rxAxis) g_vj.SetAxis(toAxisPM(gp.sThumbRX / 32767.0), id, g_cfg.rxAxis);
            if (g_cfg.ryAxis) g_vj.SetAxis(toAxisPM(gp.sThumbRY / 32767.0), id, g_cfg.ryAxis);

            if (gp.wButtons != prevButtons) {
                for (int i = 0; i < kNumButtons; ++i) {
                    int b = g_cfg.btnMap[i];
                    if (b <= 0 || b > nBtn) continue;
                    bool down = (gp.wButtons & kButtons[i].mask) != 0;
                    if (((prevButtons & kButtons[i].mask) != 0) != down || prevButtons == 0xFFFF)
                        g_vj.SetBtn(down, id, (UCHAR)b);
                }
                if (g_cfg.pov) updatePov(id, gp.wButtons, cont, disc);
                prevButtons = gp.wButtons;
            }
        }

        // ---- force feedback ----
        if (ffbOn && now - lastFfb >= (DWORD)g_cfg.ffbRateMs) {
            lastFfb = now;
            lastForce = computeForce(rawPos, vel, accel, now);
            XVibration v; forceToMotors(lastForce, v);
            if (v.wLeftMotorSpeed != lastVib.wLeftMotorSpeed || v.wRightMotorSpeed != lastVib.wRightMotorSpeed) {
                if (g_xi.SetState(g_activeSlot, &v) == ERROR_SUCCESS) lastVib = v;
            }
        }

        if (g_cfg.status && now - lastStatus >= 100) {
            lastStatus = now;
            printf("\rSterzo %+5.0f%%  Gas %3.0f%%  Freno %3.0f%%  FFB %+4.0f%%  pkt %ld   ",
                   curPos * 100, acc * 100, brk * 100, lastForce * 100, (long)g_ffbPackets);
            fflush(stdout);
        }
        Sleep(g_cfg.pollMs);
    }

    printf("\nChiusura...\n");
    if (g_activeSlot >= 0) { XVibration z{ 0, 0 }; g_xi.SetState(g_activeSlot, &z); }
    neutralVjoy(id, cont, disc);
    g_vj.RelinquishVJD(id);
    return 0;
}

// =====================================================================================
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    g_exeDir = path;
    g_exeDir.resize(g_exeDir.find_last_of(L"\\/"));
    g_iniPath = g_exeDir + L"\\madcatz_wheel.ini";

    std::wstring mode = argc > 1 ? argv[1] : L"";
    if (mode == L"--help" || mode == L"-h" || mode == L"/?") {
        printf("madcatz_wheel.exe [--list | --monitor | --ffb-test]\n");
        return 0;
    }

    printf("MadCatz Xbox 360 Wireless FF Racing Wheel -> vJoy  (v1.0)\n");
    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        printf("(madcatz_wheel.ini non trovato: uso i valori predefiniti)\n");
    loadConfig();

    if (!g_xi.load()) { printf("ERRORE: XInput non disponibile.\n"); return 1; }

    if (mode == L"--list")     return runList();
    if (mode == L"--monitor")  return runMonitor();
    if (mode == L"--ffb-test") return runFfbTest();

    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\MadCatzWheelBridge");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { printf("Il programma e' gia' in esecuzione.\n"); return 1; }

    InitializeCriticalSection(&g_ffbLock);
    timeBeginPeriod(1);
    int rc = runBridge();
    timeEndPeriod(1);
    DeleteCriticalSection(&g_ffbLock);
    if (mtx) CloseHandle(mtx);
    return rc;
}
