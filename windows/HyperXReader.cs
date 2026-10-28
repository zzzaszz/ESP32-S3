using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

public sealed class HyperXReading {
    public int Level = -1;
    public int Charging = -1;
    public string Diagnostic = "";
}

public static class HyperXReader {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("hid.dll", SetLastError=true)]
    static extern bool HidD_SetOutputReport(SafeFileHandle handle, byte[] report, uint length);

    // Accept only a response to the current GET, not an unrelated notification
    // queued while Windows was opening the HID collections.
    public static int ParseReply(byte[] data, int count, byte command) {
        if (data == null || count < 7 || count > data.Length || data[0] != 0x0C || data[5] != command) return -1;
        int value = data[6];
        if (command == 0x06 && value <= 100) return value;
        if (command == 0x48 && value <= 2) return value;
        return -1;
    }

    sealed class Endpoint : IDisposable {
        public FileStream Stream;
        public byte[] Buffer = new byte[512];
        public Task<int> Pending;
        public void Read() { Pending = Stream.ReadAsync(Buffer, 0, Buffer.Length); }
        public void Dispose() { Stream.Dispose(); }
    }

    public static HyperXReading Poll(string[] paths) {
        var result = new HyperXReading();
        var endpoints = new List<Endpoint>();
        try {
            foreach (string path in paths) {
                // Only the explicitly supported VID/PID may receive these GET reports.
                if (path.IndexOf("vid_03f0&pid_02cc", StringComparison.OrdinalIgnoreCase) < 0) continue;
                var handle = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0x40000000, IntPtr.Zero);
                if (handle.IsInvalid) { handle.Dispose(); continue; }
                try {
                    var endpoint = new Endpoint { Stream = new FileStream(handle, FileAccess.ReadWrite, 512, true) };
                    endpoints.Add(endpoint);
                    endpoint.Read();
                } catch { handle.Dispose(); }
            }
            foreach (byte command in new byte[] { 0x06, 0x48 }) {
                // Discard buffered reports before this request. All collections
                // remain open so the reply can arrive on a different handle.
                foreach (var endpoint in endpoints) {
                    int drained = 0;
                    while (endpoint.Pending != null && endpoint.Pending.IsCompleted && drained++ < 32) {
                        try { endpoint.Pending.GetAwaiter().GetResult(); endpoint.Read(); }
                        catch { endpoint.Pending = null; }
                    }
                }
                byte[] request = new byte[64];
                request[0]=0x0C; request[1]=2; request[2]=3; request[3]=1; request[5]=command;
                bool sent = false;
                foreach (var endpoint in endpoints) {
                    // SET_REPORT Output (0x020C), verified with this USB receiver.
                    if (HidD_SetOutputReport(endpoint.Stream.SafeFileHandle, request, 64)) { sent=true; break; }
                }
                if (!sent) { result.Diagnostic += "GET " + command.ToString("X2") + " refused; "; continue; }
                var timer = Stopwatch.StartNew();
                bool answered = false;
                while (timer.ElapsedMilliseconds < 1200 && !answered) {
                    foreach (var endpoint in endpoints) {
                        if (endpoint.Pending == null || !endpoint.Pending.IsCompleted) continue;
                        try {
                            int count = endpoint.Pending.GetAwaiter().GetResult();
                            byte[] b = endpoint.Buffer;
                            if (count >= 7 && b[0] == 0x0C && b[5] == command) {
                                int value = ParseReply(b, count, command);
                                if (command == 6) result.Level = value;
                                else result.Charging = value;
                                result.Diagnostic += "GET " + command.ToString("X2") + " raw=" + b[6] + "; ";
                                answered=true;
                            }
                            endpoint.Read();
                        } catch { endpoint.Pending=null; }
                    }
                    Thread.Sleep(10);
                }
                if (!answered) result.Diagnostic += "GET " + command.ToString("X2") + " timeout; ";
            }
        } finally { foreach (var endpoint in endpoints) endpoint.Dispose(); }
        return result;
    }
}
