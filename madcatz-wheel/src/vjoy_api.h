// vjoy_api.h
// Dichiarazioni minime dell'API di vJoy (vJoyInterface.dll, v2.1.9.x) caricate
// dinamicamente a runtime: non serve l'SDK di vJoy per compilare.
// Le strutture ricalcano public.h di vJoy 2.1.9 (packing di default).
#pragma once
#include <windows.h>

namespace vj {

// Assi HID (usage page Generic Desktop)
enum Axis : UINT {
    HID_X  = 0x30, HID_Y  = 0x31, HID_Z  = 0x32,
    HID_RX = 0x33, HID_RY = 0x34, HID_RZ = 0x35,
    HID_SL0 = 0x36, HID_SL1 = 0x37,
};

enum VjdStat { VJD_STAT_OWN, VJD_STAT_FREE, VJD_STAT_BUSY, VJD_STAT_MISS, VJD_STAT_UNKN };

// ---------- Force Feedback ----------
struct FFB_DATA { ULONG size; ULONG cmd; UCHAR* data; };

enum FFBPType {
    PT_EFFREP = 0x01, PT_ENVREP = 0x02, PT_CONDREP = 0x03, PT_PRIDREP = 0x04,
    PT_CONSTREP = 0x05, PT_RAMPREP = 0x06, PT_CSTMREP = 0x07, PT_SMPLREP = 0x08,
    PT_EFOPREP = 0x0A, PT_BLKFRREP = 0x0B, PT_CTRLREP = 0x0C, PT_GAINREP = 0x0D,
    PT_SETCREP = 0x0E,
    PT_NEWEFREP = 0x11, PT_BLKLDREP = 0x12, PT_POOLREP = 0x13,
};

enum FFBEType {
    ET_NONE = 0, ET_CONST = 1, ET_RAMP = 2, ET_SQR = 3, ET_SINE = 4, ET_TRNGL = 5,
    ET_STUP = 6, ET_STDN = 7, ET_SPRNG = 8, ET_DMPR = 9, ET_INRT = 10,
    ET_FRCTN = 11, ET_CSTM = 12,
};

enum FFBOP { EFF_START = 1, EFF_SOLO = 2, EFF_STOP = 3 };

enum FFB_CTRL {
    CTRL_ENACT = 1, CTRL_DISACT = 2, CTRL_STOPALL = 3,
    CTRL_DEVRST = 4, CTRL_DEVPAUSE = 5, CTRL_DEVCONT = 6,
};

struct FFB_EFF_REPORT {
    BYTE     EffectBlockIndex;
    FFBEType EffectType;
    WORD     Duration;      // ms, 0xFFFF = infinito
    WORD     TrigerRpt;
    WORD     SamplePrd;
    BYTE     Gain;
    BYTE     TrigerBtn;
    BOOL     Polar;         // TRUE: Direction polare (0x00-0xFF = 0-360°)
    union { BYTE Direction; BYTE DirX; };
    BYTE     DirY;
};

struct FFB_EFF_CONSTANT { BYTE EffectBlockIndex; LONG Magnitude; };          // -10000..10000
struct FFB_EFF_RAMP     { BYTE EffectBlockIndex; LONG Start; LONG End; };    // -10000..10000
struct FFB_EFF_PERIOD   { BYTE EffectBlockIndex; DWORD Magnitude; LONG Offset; DWORD Phase; DWORD Period; };
struct FFB_EFF_COND {
    BYTE  EffectBlockIndex;
    BOOL  isY;
    LONG  CenterPointOffset;  // -10000..10000
    LONG  PosCoeff;           // -10000..10000
    LONG  NegCoeff;           // -10000..10000
    DWORD PosSatur;           // 0..10000
    DWORD NegSatur;           // 0..10000
    LONG  DeadBand;           // 0..10000
};
struct FFB_EFF_OP { BYTE EffectBlockIndex; FFBOP EffectOp; BYTE LoopCount; };

typedef void (CALLBACK* FfbGenCB)(PVOID data, PVOID userData);

// ---------- Puntatori a funzione ----------
struct Api {
    HMODULE dll = nullptr;

    BOOL    (__cdecl* vJoyEnabled)() = nullptr;
    VjdStat (__cdecl* GetVJDStatus)(UINT) = nullptr;
    BOOL    (__cdecl* AcquireVJD)(UINT) = nullptr;
    VOID    (__cdecl* RelinquishVJD)(UINT) = nullptr;
    BOOL    (__cdecl* ResetVJD)(UINT) = nullptr;
    BOOL    (__cdecl* SetAxis)(LONG, UINT, UINT) = nullptr;
    BOOL    (__cdecl* SetBtn)(BOOL, UINT, UCHAR) = nullptr;
    BOOL    (__cdecl* SetContPov)(DWORD, UINT, UCHAR) = nullptr;
    BOOL    (__cdecl* SetDiscPov)(int, UINT, UCHAR) = nullptr;
    BOOL    (__cdecl* GetVJDAxisExist)(UINT, UINT) = nullptr;
    int     (__cdecl* GetVJDButtonNumber)(UINT) = nullptr;
    int     (__cdecl* GetVJDContPovNumber)(UINT) = nullptr;
    int     (__cdecl* GetVJDDiscPovNumber)(UINT) = nullptr;
    BOOL    (__cdecl* IsDeviceFfb)(UINT) = nullptr;

    VOID    (__cdecl* FfbRegisterGenCB)(FfbGenCB, PVOID) = nullptr;
    DWORD   (__cdecl* Ffb_h_DeviceID)(const FFB_DATA*, int*) = nullptr;
    DWORD   (__cdecl* Ffb_h_Type)(const FFB_DATA*, FFBPType*) = nullptr;
    DWORD   (__cdecl* Ffb_h_EBI)(const FFB_DATA*, int*) = nullptr;
    DWORD   (__cdecl* Ffb_h_Eff_Report)(const FFB_DATA*, FFB_EFF_REPORT*) = nullptr;
    DWORD   (__cdecl* Ffb_h_Eff_Constant)(const FFB_DATA*, FFB_EFF_CONSTANT*) = nullptr;
    DWORD   (__cdecl* Ffb_h_Eff_Ramp)(const FFB_DATA*, FFB_EFF_RAMP*) = nullptr;
    DWORD   (__cdecl* Ffb_h_Eff_Period)(const FFB_DATA*, FFB_EFF_PERIOD*) = nullptr;
    DWORD   (__cdecl* Ffb_h_Eff_Cond)(const FFB_DATA*, FFB_EFF_COND*) = nullptr;
    DWORD   (__cdecl* Ffb_h_EffOp)(const FFB_DATA*, FFB_EFF_OP*) = nullptr;
    DWORD   (__cdecl* Ffb_h_DevCtrl)(const FFB_DATA*, FFB_CTRL*) = nullptr;
    DWORD   (__cdecl* Ffb_h_DevGain)(const FFB_DATA*, BYTE*) = nullptr;

    template <class F> bool get(F& fn, const char* name) {
        fn = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(dll, name)));
        return fn != nullptr;
    }

    // Ritorna true se la DLL e tutte le funzioni base sono disponibili.
    // ffbOk viene impostato a true se sono presenti anche le funzioni FFB.
    bool load(const wchar_t* exeDir, bool& ffbOk) {
        const wchar_t* candidates[] = {
            L"vJoyInterface.dll",
            L"C:\\Program Files\\vJoy\\x64\\vJoyInterface.dll",
            L"C:\\Program Files (x86)\\vJoy\\x64\\vJoyInterface.dll",
        };
        wchar_t local[MAX_PATH];
        wsprintfW(local, L"%s\\vJoyInterface.dll", exeDir);
        dll = LoadLibraryW(local);
        for (auto c : candidates) { if (dll) break; dll = LoadLibraryW(c); }
        if (!dll) return false;

        bool ok = get(vJoyEnabled, "vJoyEnabled") && get(GetVJDStatus, "GetVJDStatus") &&
                  get(AcquireVJD, "AcquireVJD") && get(RelinquishVJD, "RelinquishVJD") &&
                  get(ResetVJD, "ResetVJD") && get(SetAxis, "SetAxis") &&
                  get(SetBtn, "SetBtn") && get(SetContPov, "SetContPov") &&
                  get(SetDiscPov, "SetDiscPov") && get(GetVJDAxisExist, "GetVJDAxisExist") &&
                  get(GetVJDButtonNumber, "GetVJDButtonNumber") &&
                  get(GetVJDContPovNumber, "GetVJDContPovNumber") &&
                  get(GetVJDDiscPovNumber, "GetVJDDiscPovNumber");

        ffbOk = get(IsDeviceFfb, "IsDeviceFfb") && get(FfbRegisterGenCB, "FfbRegisterGenCB") &&
                get(Ffb_h_DeviceID, "Ffb_h_DeviceID") && get(Ffb_h_Type, "Ffb_h_Type") &&
                get(Ffb_h_EBI, "Ffb_h_EBI") && get(Ffb_h_Eff_Report, "Ffb_h_Eff_Report") &&
                get(Ffb_h_Eff_Constant, "Ffb_h_Eff_Constant") &&
                get(Ffb_h_Eff_Ramp, "Ffb_h_Eff_Ramp") && get(Ffb_h_Eff_Period, "Ffb_h_Eff_Period") &&
                get(Ffb_h_Eff_Cond, "Ffb_h_Eff_Cond") && get(Ffb_h_EffOp, "Ffb_h_EffOp") &&
                get(Ffb_h_DevCtrl, "Ffb_h_DevCtrl") && get(Ffb_h_DevGain, "Ffb_h_DevGain");
        return ok;
    }
};

} // namespace vj
