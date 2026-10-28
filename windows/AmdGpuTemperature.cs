using System;
using System.Runtime.InteropServices;

public static class AmdGpuTemperature {
    public static string Diagnostic = "not queried";
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate IntPtr Allocate(int bytes);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int CreateContext(Allocate allocator, int connectedOnly, out IntPtr context);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int AdapterCount(IntPtr context, out int count);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int Temperature(IntPtr context, int adapter, int temperatureType, out int milliCelsius);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int QuerySensors(IntPtr context, int adapter, IntPtr output);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int DestroyContext(IntPtr context);

    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibrary(string path);
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi, ExactSpelling=true, SetLastError=true)]
    static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool FreeLibrary(IntPtr module);

    static T Load<T>(IntPtr module, string name) where T : class {
        IntPtr address = GetProcAddress(module, name);
        return address == IntPtr.Zero ? null : Marshal.GetDelegateForFunctionPointer(address, typeof(T)) as T;
    }

    static IntPtr AllocateMemory(int bytes) {
        return Marshal.AllocHGlobal(bytes);
    }

    public static int ReadCelsius() {
        IntPtr module = LoadLibrary(System.IO.Path.Combine(Environment.SystemDirectory, "atiadlxx.dll"));
        if (module == IntPtr.Zero) { Diagnostic = "AMD driver library unavailable"; return -1; }
        IntPtr context = IntPtr.Zero;
        Allocate allocator = AllocateMemory;
        try {
            CreateContext create = Load<CreateContext>(module, "ADL2_Main_Control_Create");
            AdapterCount getCount = Load<AdapterCount>(module, "ADL2_Adapter_NumberOfAdapters_Get");
            Temperature getTemperature = Load<Temperature>(module, "ADL2_OverdriveN_Temperature_Get");
            QuerySensors querySensors = Load<QuerySensors>(module, "ADL2_New_QueryPMLogData_Get");
            DestroyContext destroy = Load<DestroyContext>(module, "ADL2_Main_Control_Destroy");
            int count = 0;
            if (create == null || getCount == null || destroy == null) {
                Diagnostic = "AMD context API unavailable"; return -1;
            }
            int code = create(allocator, 1, out context);
            if (code != 0 || context == IntPtr.Zero) { Diagnostic = "AMD context result=" + code; return -1; }
            code = getCount(context, out count);
            if (code != 0 || count < 1 || count > 64) { Diagnostic = "AMD adapter count=" + count + " result=" + code; return -1; }
            Diagnostic = "AMD adapters=" + count;
            for (int adapter = 0; adapter < count; ++adapter) {
                // Documented ADLPMLogDataOutput: size + 256 {supported,value}
                // entries. Sensor 8 is GPU edge temperature in degrees Celsius.
                if (querySensors != null) {
                    const int bytes = 4 + 256 * 8;
                    IntPtr output = Marshal.AllocHGlobal(bytes);
                    try {
                        Marshal.Copy(new byte[bytes], 0, output, bytes);
                        Marshal.WriteInt32(output, bytes);
                        code = querySensors(context, adapter, output);
                        if (code == 0 && Marshal.ReadInt32(output, 4 + 8 * 8) != 0) {
                            int value = Marshal.ReadInt32(output, 4 + 8 * 8 + 4);
                            if (value >= 0 && value <= 150) {
                                Diagnostic = "AMD PMLog GPU edge adapter=" + adapter;
                                return value;
                            }
                        }
                        Diagnostic += "; PMLog[" + adapter + "]=" + code;
                    } finally { Marshal.FreeHGlobal(output); }
                }
                int milliCelsius;
                if (getTemperature != null && getTemperature(context, adapter, 1, out milliCelsius) == 0 &&
                    milliCelsius >= 0 && milliCelsius <= 150000) {
                    Diagnostic = "AMD OverdriveN GPU core adapter=" + adapter;
                    return (milliCelsius + 500) / 1000;
                }
            }
            return -1;
        } catch (Exception e) { Diagnostic = e.GetType().Name; return -1; }
        finally {
            if (context != IntPtr.Zero) {
                try {
                    DestroyContext destroy = Load<DestroyContext>(module, "ADL2_Main_Control_Destroy");
                    if (destroy != null) destroy(context);
                } catch { }
            }
            FreeLibrary(module);
            GC.KeepAlive(allocator);
        }
    }
}
