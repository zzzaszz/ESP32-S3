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
                byte[] request = new byte[64];
                request[0]=0x0C; request[1]=2; request[2]=3; request[3]=1; request[5]=command;
                bool sent = false;
                foreach (var endpoint in endpoints) {
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
                            int kind=-1, value=-1;
                            if (count>=7 && b[0]==0x0C) { kind=b[5]; value=b[6]; }
                            if (count>=6 && b[0]==0x0D) {
                                if (b[4]==1) { kind=6; value=b[5]; }
                                if (b[4]==10) { kind=0x48; value=b[5]; }
                            }
                            if (kind==6 && value>=0 && value<=100) result.Level=value;
                            if (kind==0x48 && value>=0 && value<=2) result.Charging=value;
                            if (kind==command) {
                                result.Diagnostic += "GET " + command.ToString("X2") + " reply=" + value + "; ";
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
