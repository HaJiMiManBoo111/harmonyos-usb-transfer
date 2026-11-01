using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace Lingxi
{
    static class Program
    {
        private static string _logFile = null;

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AttachConsole(int dwProcessId);

        private const int ATTACH_PARENT_PROCESS = -1;

        // winexe 默认没有控制台；从 cmd/bash 启动时挂到父进程控制台，让 LIST|/PULL| 行能被父进程读到
        private static void AttachParentConsole()
        {
            // 挂父进程控制台只是为了让交互运行时能看到输出；失败不影响功能
            try { AttachConsole(ATTACH_PARENT_PROCESS); } catch { }

            // 关键：输出被重定向（管道/文件）时，.NET 默认用 Console.OutputEncoding（中文系统是 GBK/936）
            // 编码字节，中文名会变成 GBK 乱码，子进程接收方按 UTF-8 读必然对不上。
            // 这里在重定向时改用 UTF-8 字节流。未重定向时不接管，让 .NET 走 WriteConsoleW 保证控制台显示正常。
            try
            {
                if (Console.IsOutputRedirected)
                {
                    StreamWriter so = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false));
                    so.AutoFlush = true;
                    Console.SetOut(so);
                }
                if (Console.IsErrorRedirected)
                {
                    StreamWriter se = new StreamWriter(Console.OpenStandardError(), new UTF8Encoding(false));
                    se.AutoFlush = true;
                    Console.SetError(se);
                }
            }
            catch { }
        }

        [STAThread]
        static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Fluent.InitDpi();

            if (args != null && args.Length > 0)
            {
                AttachParentConsole();
                int rc = RunSilent(args);
                Environment.ExitCode = rc;
                return;
            }

            Application.Run(new MainForm());
        }

        // 全过程写日志文件（--log 指定）
        private static void LogLine(string line)
        {
            Console.WriteLine(line);
            if (_logFile == null) return;
            try
            {
                using (StreamWriter sw = new StreamWriter(_logFile, true, new UTF8Encoding(false)))
                {
                    sw.WriteLine(line);
                }
            }
            catch { }
        }

        private static int RunSilent(string[] args)
        {
            List<string> paths = new List<string>();
            List<string> remotes = new List<string>();
            string mode = null;      // "pull" | "list"
            string toDir = null;

            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i];
                if (string.IsNullOrEmpty(a)) continue;

                if (string.Equals(a, "--pull", StringComparison.OrdinalIgnoreCase)) { mode = "pull"; continue; }
                if (string.Equals(a, "--list", StringComparison.OrdinalIgnoreCase)) { mode = "list"; continue; }

                if (string.Equals(a, "--to", StringComparison.OrdinalIgnoreCase))
                {
                    if (i + 1 < args.Length) { toDir = args[i + 1]; i++; }
                    continue;
                }
                if (string.Equals(a, "--log", StringComparison.OrdinalIgnoreCase))
                {
                    if (i + 1 < args.Length) { _logFile = args[i + 1]; i++; }
                    continue;
                }
                if (a.StartsWith("-")) continue;

                if (mode == "pull" || mode == "list") remotes.Add(a);
                else paths.Add(a);
            }

            if (mode == "list") return RunList(remotes);
            if (mode == "pull") return RunPull(remotes, toDir);
            return RunSend(paths);
        }

        private static int RunList(List<string> remotes)
        {
            string dir = remotes.Count > 0 ? remotes[0] : HdcCore.RemoteDesktop;
            try
            {
                RemoteItem[] items = HdcCore.ListRemote(dir);
                for (int i = 0; i < items.Length; i++)
                {
                    LogLine(string.Format("LIST|{0}|{1}|{2}",
                        items[i].IsDir ? "D" : "F", items[i].Name, items[i].Size));
                }
                LogLine(string.Format("LIST|END|{0}", items.Length));
                return 0;
            }
            catch (TransferException ex)
            {
                // 前缀必须是 LIST|FAIL|，和 --pull 的 PULL|FAIL| 区分开，否则排障时 grep 不到列表失败。
                LogLine("LIST|FAIL|" + ex.Message);
                return 1;
            }
            catch (Exception ex2)
            {
                LogLine("LIST|FAIL|" + ex2.Message);
                return 1;
            }
        }

        private static int RunPull(List<string> remotes, string toDir)
        {
            if (remotes.Count == 0)
            {
                LogLine("PULL|FAIL|没有指定要从鸿蒙接收的路径");
                return 1;
            }
            try
            {
                TransferResult r = HdcCore.Pull(remotes.ToArray(), toDir, null,
                    delegate(string line) { Console.Error.WriteLine(line); });
                if (r.Success)
                {
                    LogLine("PULL|OK|" + r.Message);
                    return 0;
                }
                LogLine("PULL|FAIL|" + r.Message);
                return 1;
            }
            catch (TransferException ex)
            {
                LogLine("PULL|FAIL|" + ex.Message);
                return 1;
            }
            catch (Exception ex2)
            {
                LogLine("PULL|FAIL|" + ex2.Message);
                return 1;
            }
        }

        private static int RunSend(List<string> paths)
        {
            if (paths.Count == 0)
            {
                MessageBox.Show("没有收到文件。\n\n把文件拖到这个程序的图标上，或先运行程序再拖进窗口。",
                    "灵犀", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return 1;
            }

            TransferResult r;
            try
            {
                r = HdcCore.Send(paths.ToArray(), HdcCore.RemoteDesktop, null, null);
            }
            catch (TransferException ex)
            {
                MessageBox.Show(ex.Message, "灵犀", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return 1;
            }
            catch (Exception ex2)
            {
                MessageBox.Show("出错了：" + ex2.Message, "灵犀", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }

            if (r.Success)
            {
                MessageBox.Show(r.Message, "灵犀 · 成功", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return 0;
            }
            MessageBox.Show(r.Message, "灵犀 · 失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }
}
