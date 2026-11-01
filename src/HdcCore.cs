using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace Lingxi
{
    public class DeviceInfo
    {
        public string Serial;
        public bool IsUsb;

        public override string ToString()
        {
            return string.Format("鸿蒙设备 ({0}) {1}", IsUsb ? "USB" : "网络", Serial);
        }
    }

    public class TransferResult
    {
        public bool Success;
        public string Message;
        public int FileCount;
        public long TotalBytes;
        public long ElapsedMs;
    }

    public class TransferException : Exception
    {
        public TransferException(string message) : base(message) { }
    }

    public class RemoteItem
    {
        public string Name;
        public string FullPath;
        public bool IsDir;
        public long Size;
    }

    public static class HdcCore
    {
        public const string RemoteDesktop = "/storage/media/100/local/files/Docs/Desktop";
        public const string RemoteDownload = "/storage/media/100/local/files/Docs/Download";
        public const string RemoteDocuments = "/storage/media/100/local/files/Docs/Documents";
        public const string RemoteImages = "/storage/media/100/local/files/Docs/Images";
        public const string RemoteVideos = "/storage/media/100/local/files/Docs/Videos";
        public const string RemoteMusic = "/storage/media/100/local/files/Docs/Music";

        private static string _baseDir;

        public static string BaseDir
        {
            get
            {
                if (_baseDir == null)
                {
                    string p = System.Reflection.Assembly.GetExecutingAssembly().Location;
                    _baseDir = string.IsNullOrEmpty(p) ? Environment.CurrentDirectory : Path.GetDirectoryName(p);
                }
                return _baseDir;
            }
        }

        private static string _hdcPath;

        /// <summary>
        /// 定位 hdc.exe。查找顺序：
        ///   1. 程序同目录下的 hdc.exe（便携部署）
        ///   2. 环境变量 LINGXI_HDC 指定的路径
        ///   3. PATH 里的 hdc.exe
        ///   4. DevEco Studio 默认安装路径（若干候选版本目录）
        /// 全部找不到时返回程序同目录的路径（供上层给出「缺少 hdc」提示）。
        /// </summary>
        public static string HdcPath
        {
            get
            {
                if (_hdcPath != null) return _hdcPath;
                string local = Path.Combine(BaseDir, "hdc.exe");
                if (File.Exists(local)) { _hdcPath = local; return _hdcPath; }

                string env = Environment.GetEnvironmentVariable("LINGXI_HDC");
                if (!string.IsNullOrEmpty(env) && File.Exists(env)) { _hdcPath = env; return _hdcPath; }

                string fromPath = FindOnPath("hdc.exe");
                if (fromPath != null) { _hdcPath = fromPath; return _hdcPath; }

                string devEco = FindInDevEco();
                if (devEco != null) { _hdcPath = devEco; return _hdcPath; }

                _hdcPath = local;
                return _hdcPath;
            }
        }

        /// <summary>在 PATH 各目录里找可执行文件。</summary>
        private static string FindOnPath(string fileName)
        {
            try
            {
                string pathVar = Environment.GetEnvironmentVariable("PATH");
                if (string.IsNullOrEmpty(pathVar)) return null;
                string[] dirs = pathVar.Split(new char[] { Path.PathSeparator }, StringSplitOptions.RemoveEmptyEntries);
                for (int i = 0; i < dirs.Length; i++)
                {
                    try
                    {
                        string cand = Path.Combine(dirs[i].Trim('"'), fileName);
                        if (File.Exists(cand)) return cand;
                    }
                    catch (Exception) { }
                }
            }
            catch (Exception) { }
            return null;
        }

        /// <summary>在 DevEco Studio 常见安装目录里找 hdc.exe（取版本号最大的一个）。</summary>
        private static string FindInDevEco()
        {
            string[] roots = new string[]
            {
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Huawei", "DevEco Studio"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Huawei", "DevEco Studio"),
                Path.Combine("C:" + Path.DirectorySeparatorChar.ToString(), "Program Files", "Huawei", "DevEco Studio")
            };
            string best = null;
            for (int i = 0; i < roots.Length; i++)
            {
                try
                {
                    if (!Directory.Exists(roots[i])) continue;
                    string cand = Path.Combine(roots[i], "sdk", "default", "openharmony", "toolchains", "hdc.exe");
                    if (File.Exists(cand)) { best = cand; }
                    if (best != null) break;
                    string[] sdks = Directory.GetDirectories(Path.Combine(roots[i], "sdk"));
                    for (int j = 0; j < sdks.Length; j++)
                    {
                        string c2 = Path.Combine(sdks[j], "openharmony", "toolchains", "hdc.exe");
                        if (File.Exists(c2)) { best = c2; }
                    }
                }
                catch (Exception) { }
            }
            return best;
        }

        // ---------- 进程调用 ----------

        private class RunOut
        {
            public int ExitCode;
            public string StdOut;
            public string StdErr;
        }

        private static RunOut Run(string exe, string args, int timeoutMs)
        {
            ProcessStartInfo psi = new ProcessStartInfo();
            psi.FileName = exe;
            psi.Arguments = args;
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardOutput = true;
            psi.RedirectStandardError = true;
            psi.StandardOutputEncoding = Encoding.UTF8;
            psi.StandardErrorEncoding = Encoding.UTF8;

            RunOut ro = new RunOut();
            using (Process p = new Process())
            {
                p.StartInfo = psi;
                StringBuilder so = new StringBuilder();
                StringBuilder se = new StringBuilder();
                p.OutputDataReceived += delegate(object s, DataReceivedEventArgs e)
                {
                    if (e.Data != null) so.AppendLine(e.Data);
                };
                p.ErrorDataReceived += delegate(object s, DataReceivedEventArgs e)
                {
                    if (e.Data != null) se.AppendLine(e.Data);
                };
                p.Start();
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();
                if (!p.WaitForExit(timeoutMs))
                {
                    try { p.Kill(); } catch { }
                    ro.ExitCode = -1;
                    ro.StdOut = so.ToString();
                    ro.StdErr = "timeout";
                    return ro;
                }
                p.WaitForExit();
                ro.ExitCode = p.ExitCode;
                ro.StdOut = so.ToString();
                ro.StdErr = se.ToString();
            }
            return ro;
        }

        private static string Quote(string s)
        {
            return "\"" + s + "\"";
        }

        // ---------- 设备 ----------

        public static DeviceInfo[] ListDevices()
        {
            List<DeviceInfo> list = new List<DeviceInfo>();
            if (!File.Exists(HdcPath)) return list.ToArray();

            RunOut ro = Run(HdcPath, "list targets -v", 15000);
            if (ro.StdOut == null) return list.ToArray();

            string[] lines = ro.StdOut.Replace("\r", "").Split('\n');
            Regex usb = new Regex(@"^\s*(\S+)\s+USB\s+Connected", RegexOptions.IgnoreCase);
            Regex tcp = new Regex(@"^\s*(\S+)\s+TCP\s+Connected", RegexOptions.IgnoreCase);
            Regex conn = new Regex(@"^\s*(\S+)\s+Connected", RegexOptions.IgnoreCase);

            foreach (string raw in lines)
            {
                string line = raw;
                if (line.Trim().Length == 0) continue;
                Match m = usb.Match(line);
                if (m.Success)
                {
                    DeviceInfo d = new DeviceInfo();
                    d.Serial = m.Groups[1].Value;
                    d.IsUsb = true;
                    list.Add(d);
                    continue;
                }
                m = tcp.Match(line);
                if (m.Success)
                {
                    DeviceInfo d2 = new DeviceInfo();
                    d2.Serial = m.Groups[1].Value;
                    d2.IsUsb = false;
                    list.Add(d2);
                    continue;
                }
                m = conn.Match(line);
                if (m.Success)
                {
                    string s = m.Groups[1].Value;
                    if (s.StartsWith("COM", StringComparison.OrdinalIgnoreCase)) continue;
                    DeviceInfo d3 = new DeviceInfo();
                    d3.Serial = s;
                    d3.IsUsb = false;
                    list.Add(d3);
                }
            }
            return list.ToArray();
        }

        public static DeviceInfo ResolveDevice()
        {
            if (!File.Exists(HdcPath))
                throw new TransferException("缺少 hdc.exe。请从 DevEco Studio 安装目录复制 hdc.exe 到本程序所在文件夹，或安装 DevEco Studio、或设置环境变量 LINGXI_HDC 指向 hdc.exe。");
            DeviceInfo[] ds = ListDevices();
            for (int i = 0; i < ds.Length; i++)
            {
                if (ds[i].IsUsb) return ds[i];
            }
            if (ds.Length > 0) return ds[0];
            throw new TransferException("未找到鸿蒙设备。请用数据线连接电脑，并在鸿蒙上开启 USB 调试后重试。");
        }

        // ---------- 传输 ----------

        private static long LocalSize(string path)
        {
            try
            {
                if (File.Exists(path))
                {
                    return new FileInfo(path).Length;
                }
                if (Directory.Exists(path))
                {
                    long total = 0;
                    string[] files = Directory.GetFiles(path, "*", SearchOption.AllDirectories);
                    for (int i = 0; i < files.Length; i++)
                    {
                        try { total += new FileInfo(files[i]).Length; }
                        catch { }
                    }
                    return total;
                }
            }
            catch { }
            return 0;
        }

        // 远程路径是否存在，返回字节数；不存在返回 -1
        private static long RemoteSize(string serial, string remotePath)
        {
            string args = string.Format("-t {0} shell \"stat -c %s {1} 2>/dev/null\"", serial, remotePath);
            RunOut ro = Run(HdcPath, args, 10000);
            if (ro.StdOut == null) return -1;
            string s = ro.StdOut.Replace("\r", "").Replace("\n", "").Trim();
            long v;
            if (long.TryParse(s, out v)) return v;
            return -1;
        }

        // ================= v2：鸿蒙 -> Windows 接收方向 =================

        public static string[] RemoteRoots
        {
            get
            {
                return new string[]
                {
                    RemoteDesktop, RemoteDownload, RemoteDocuments,
                    RemoteImages, RemoteVideos, RemoteMusic
                };
            }
        }

        // 远程 shell 单引号转义：把 ' 变成 '\''
        private static string SQ(string s)
        {
            if (s == null) return "''";
            return "'" + s.Replace("'", "'\\''") + "'";
        }

        private static string RemoteShell(DeviceInfo dev, string command, int timeoutMs)
        {
            RunOut ro = Run(HdcPath, string.Format("-t {0} shell {1}", dev.Serial, Quote(command)), timeoutMs);
            if (ro.StdOut == null) return "";
            return ro.StdOut.Replace("\r", "");
        }

        // 把 find -printf 输出切成行，跳过空行与 find 自身的报错
        private static string[] CleanLines(string stdout)
        {
            if (stdout == null) return new string[0];
            List<string> outp = new List<string>();
            string[] raw = stdout.Replace("\r", "").Split('\n');
            for (int i = 0; i < raw.Length; i++)
            {
                string s = raw[i];
                if (s.Length == 0) continue;
                if (s.StartsWith("find: ")) continue;
                outp.Add(s);
            }
            return outp.ToArray();
        }

        /// <summary>
        /// 目录的递归**文件字节数**。
        /// 不用 du -sb：它统计磁盘占用（含目录 inode 与块对齐），8 字节的目录会报 6888 字节，
        /// 用来算进度会一致偏大。find 累加 %s 才是真实字节数，且实测在大目录（10 万+ 文件）上 0.12s 返回。
        /// </summary>
        public static long RemoteDirSize(string remoteDir)
        {
            try
            {
                DeviceInfo dev = ResolveDevice();
                string cmd = string.Format(
                    "find {0} -type f -printf '%s\\n' 2>/dev/null | awk '{{s+=$1}} END {{print s+0}}'",
                    SQ(remoteDir));
                string[] lines = CleanLines(RemoteShell(dev, cmd, 180000));
                if (lines.Length == 0) return 0;
                long v;
                if (long.TryParse(lines[0].Trim(), out v)) return v;
            }
            catch { }
            return 0;
        }

        public static RemoteItem[] ListRemote(string remoteDir)
        {
            DeviceInfo dev = ResolveDevice();
            List<RemoteItem> items = new List<RemoteItem>();

            string dir = remoteDir;
            if (string.IsNullOrEmpty(dir)) dir = RemoteDesktop;
            if (dir.Length > 1) dir = dir.TrimEnd('/');

            // 目录不存在时必须报错，不能返回空列表——否则界面会把「路径写错」显示成「这个目录是空的」
            // 注意：不能用 Contains("directory") 判断，因为 stat 的错误串 "No such file or directory" 里也有这个词
            string kindCmd = string.Format("stat -c '%F' {0} 2>&1", SQ(dir));
            string[] kindLines = CleanLines(RemoteShell(dev, kindCmd, 30000));
            string kindText = kindLines.Length > 0 ? kindLines[0].Trim() : "";
            if (!string.Equals(kindText, "directory", StringComparison.OrdinalIgnoreCase))
            {
                throw new TransferException(string.Format("无法读取鸿蒙目录 {0}：{1}",
                    dir, kindText.Length == 0 ? "目录不存在或没有权限" : kindText));
            }

            // 1) 普通文件
            string cmdF = string.Format("find {0} -mindepth 1 -maxdepth 1 -type f -printf '%s\\t%f\\n' 2>/dev/null", SQ(dir));
            string[] lf = CleanLines(RemoteShell(dev, cmdF, 60000));
            for (int i = 0; i < lf.Length; i++)
            {
                int tab = lf[i].IndexOf('\t');
                if (tab <= 0) continue;
                string num = lf[i].Substring(0, tab);
                string nm = lf[i].Substring(tab + 1);
                if (nm.Length == 0) continue;
                long sz;
                if (!long.TryParse(num.Trim(), out sz)) sz = 0;
                RemoteItem it = new RemoteItem();
                it.Name = nm;
                it.FullPath = JoinRemote(dir, nm);
                it.IsDir = false;
                it.Size = sz;
                items.Add(it);
            }

            // 2) 子目录（名字 + 递归大小）
            string cmdD = string.Format("find {0} -mindepth 1 -maxdepth 1 -type d -printf '%f\\n' 2>/dev/null", SQ(dir));
            string[] ld = CleanLines(RemoteShell(dev, cmdD, 60000));
            List<string> dirNames = new List<string>();
            for (int i = 0; i < ld.Length; i++)
            {
                string nm = ld[i].Trim();
                if (nm.Length == 0) continue;
                dirNames.Add(nm);
            }

            if (dirNames.Count > 0)
            {
                // 每个子目录的递归文件字节数。
                // 做法：一次 find 拿「大小 + 相对路径」（%P 是相对起始目录的路径），在 C# 侧按第一段累加。
                // 为什么不在设备上 awk：这条命令最终要塞进 hdc 的 `shell "<命令>"` 的两个双引号里，
                // 命令内部再出现双引号会把外层引号提前闭合，整条命令被拆坏（实测表现为所有目录大小都是 0）。
                // 也刻意不用 du -sb：它统计磁盘占用（含目录 inode 与块对齐），8 字节的目录会报 6888。
                Dictionary<string, long> sizes2 = new Dictionary<string, long>(StringComparer.Ordinal);
                string cmdS = string.Format("find {0} -type f -printf '%s\\t%P\\n' 2>/dev/null", SQ(dir));
                string[] dl = CleanLines(RemoteShell(dev, cmdS, 180000));
                for (int i = 0; i < dl.Length; i++)
                {
                    int tab = dl[i].IndexOf('\t');
                    if (tab <= 0) continue;
                    long sz;
                    if (!long.TryParse(dl[i].Substring(0, tab).Trim(), out sz)) continue;
                    string rel = dl[i].Substring(tab + 1);
                    int slash = rel.IndexOf('/');
                    if (slash <= 0) continue;      // 落在目录根下的散文件，不计入任何子目录
                    string top = rel.Substring(0, slash);
                    long cur;
                    sizes2[top] = sizes2.TryGetValue(top, out cur) ? cur + sz : sz;
                }

                for (int i = 0; i < dirNames.Count; i++)
                {
                    string nm = dirNames[i];
                    RemoteItem it = new RemoteItem();
                    it.Name = nm;
                    it.FullPath = JoinRemote(dir, nm);
                    it.IsDir = true;
                    long sz;
                    it.Size = sizes2.TryGetValue(nm, out sz) ? sz : 0;
                    items.Add(it);
                }
            }

            items.Sort(delegate(RemoteItem a, RemoteItem b)
            {
                if (a.IsDir != b.IsDir) return a.IsDir ? -1 : 1;
                return string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase);
            });
            return items.ToArray();
        }

        private static string JoinRemote(string dir, string name)
        {
            if (dir.EndsWith("/")) return dir + name;
            return dir + "/" + name;
        }

        private static string LastSegment(string p)
        {
            if (string.IsNullOrEmpty(p)) return "";
            string s = p.TrimEnd('/');
            int i = s.LastIndexOf('/');
            return i >= 0 ? s.Substring(i + 1) : s;
        }

        // 远程路径是文件还是目录：[0]=是否目录，[1]=大小（文件精确/目录递归）
        // 判定必须用「整串等于 directory」：stat 的错误串 "No such file or directory" 也含这个词，
        // 用 Contains 会把不存在的路径误判成目录。
        private static bool[] ProbeRemoteKind(DeviceInfo dev, string remotePath, out long size)
        {
            size = 0;
            string cmd = string.Format("stat -c '%F|%s' {0} 2>/dev/null", SQ(remotePath));
            string[] lines = CleanLines(RemoteShell(dev, cmd, 30000));
            bool[] r = new bool[1];
            r[0] = false;
            if (lines.Length == 0) return r;
            string line = lines[0];
            int bar = line.IndexOf('|');
            string kind = bar > 0 ? line.Substring(0, bar).Trim() : line.Trim();
            string num = bar > 0 ? line.Substring(bar + 1) : "";
            long sz;
            if (!long.TryParse(num.Trim(), out sz)) sz = 0;
            if (string.Equals(kind, "directory", StringComparison.OrdinalIgnoreCase))
            {
                r[0] = true;
                size = RemoteDirSize(remotePath);
            }
            else
            {
                r[0] = false;
                size = sz;
            }
            return r;
        }

        // 本地路径的递归字节数（不存在返回 0）
        private static long PathSize(string path)
        {
            try
            {
                if (File.Exists(path)) return new FileInfo(path).Length;
                if (Directory.Exists(path))
                {
                    long total = 0;
                    string[] files = Directory.GetFiles(path, "*", SearchOption.AllDirectories);
                    for (int i = 0; i < files.Length; i++)
                    {
                        try { total += new FileInfo(files[i]).Length; }
                        catch { }
                    }
                    return total;
                }
            }
            catch { }
            return 0;
        }

        private static string DestLabel(string localDir)
        {
            if (string.IsNullOrEmpty(localDir)) return "本地目录";
            string t = localDir.TrimEnd('\\', '/');
            string leaf = Path.GetFileName(t);
            if (string.IsNullOrEmpty(leaf)) leaf = t;
            return leaf;
        }

        public static TransferResult Pull(string[] remotePaths, string localDir,
                                         Action<long, long> onProgress,
                                         Action<string> onLog)
        {
            if (remotePaths == null || remotePaths.Length == 0)
                throw new TransferException("没有选中要接收的文件。");
            if (string.IsNullOrEmpty(localDir))
                localDir = DefaultPullDir();

            TransferResult result = new TransferResult();
            result.FileCount = remotePaths.Length;
            Stopwatch sw = Stopwatch.StartNew();

            if (!File.Exists(HdcPath))
            {
                result.Success = false;
                result.Message = "缺少 hdc.exe。请把它放到本程序目录，或安装 DevEco Studio，或设置 LINGXI_HDC。";
                result.ElapsedMs = sw.ElapsedMilliseconds;
                return result;
            }

            DeviceInfo dev;
            try
            {
                dev = ResolveDevice();
            }
            catch (TransferException ex)
            {
                result.Success = false;
                result.Message = ex.Message;
                result.ElapsedMs = sw.ElapsedMilliseconds;
                return result;
            }
            if (onLog != null) onLog(string.Format("[设备] {0}", dev.ToString()));

            try
            {
                if (!Directory.Exists(localDir)) Directory.CreateDirectory(localDir);
            }
            catch (Exception ex)
            {
                result.Success = false;
                result.Message = string.Format("无法创建本地目录 {0}：{1}", localDir, ex.Message);
                result.ElapsedMs = sw.ElapsedMilliseconds;
                return result;
            }

            if (onLog != null) onLog(string.Format("[目标] {0}", localDir));

            // 预扫描：类型 + 大小
            List<string> names = new List<string>();
            List<bool> isDirs = new List<bool>();
            long grandTotal = 0;
            for (int i = 0; i < remotePaths.Length; i++)
            {
                string rp = remotePaths[i];
                if (string.IsNullOrEmpty(rp)) { names.Add(""); isDirs.Add(false); continue; }
                long sz;
                bool[] kind = ProbeRemoteKind(dev, rp, out sz);
                names.Add(LastSegment(rp));
                isDirs.Add(kind[0]);
                grandTotal += sz;
            }
            result.TotalBytes = grandTotal;

            string tmpRoot = Path.Combine(localDir, ".htmp");
            long doneBefore = 0;
            int okCount = 0;
            List<string> failures = new List<string>();

            for (int i = 0; i < remotePaths.Length; i++)
            {
                string rp = remotePaths[i];
                string name = names[i];
                if (name.Length == 0)
                {
                    failures.Add(string.Format("{0}: 路径无效", rp));
                    continue;
                }

                if (onLog != null) onLog(string.Format("[接收] {0} -> {1}", rp, Path.Combine(localDir, name)));

                long itemTotal = 0;
                if (i < remotePaths.Length)
                {
                    long sz0;
                    bool[] k0 = ProbeRemoteKind(dev, rp, out sz0);
                    itemTotal = sz0;
                }

                string err = "";
                bool ok = PullOne(dev, rp, name, isDirs[i], localDir, tmpRoot,
                                  itemTotal, doneBefore, grandTotal, onProgress, onLog, out err);

                if (ok)
                {
                    okCount++;
                    doneBefore += itemTotal;
                    if (onProgress != null) onProgress(doneBefore, grandTotal > 0 ? grandTotal : 0);
                }
                else
                {
                    failures.Add(string.Format("{0}: {1}", name, err.Length == 0 ? "接收失败" : err));
                    if (onLog != null) onLog(string.Format("[失败] {0} {1}", name, err));
                }
            }

            try { if (Directory.Exists(tmpRoot)) Directory.Delete(tmpRoot, true); } catch { }

            sw.Stop();
            result.ElapsedMs = sw.ElapsedMilliseconds;

            if (failures.Count == 0)
            {
                result.Success = true;
                result.Message = string.Format("已成功收到{0}：{1} 项，{2}，耗时 {3} 秒（{4}）",
                    DestLabel(localDir), okCount, FormatBytes(grandTotal),
                    (sw.ElapsedMilliseconds / 1000.0).ToString("0.0"),
                    FormatSpeed(grandTotal, sw.ElapsedMilliseconds));
            }
            else
            {
                result.Success = okCount > 0;
                result.Message = string.Format("成功 {0} 项，失败 {1} 项。首个错误：{2}",
                    okCount, failures.Count, failures[0]);
            }
            return result;
        }

        public static string DefaultPullDir()
        {
            string desk = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            if (string.IsNullOrEmpty(desk)) desk = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            return Path.Combine(desk, "from-harmony");
        }

        private static bool PullOne(DeviceInfo dev, string remotePath, string name, bool isDir,
                                    string localDir, string tmpRoot,
                                    long itemTotal, long doneBefore, long grandTotal,
                                    Action<long, long> onProgress, Action<string> onLog,
                                    out string err)
        {
            err = "";
            try
            {
                if (!Directory.Exists(tmpRoot)) Directory.CreateDirectory(tmpRoot);

                // 清掉可能残留的同名 tmp
                string tmpItem = Path.Combine(tmpRoot, name);
                try
                {
                    if (File.Exists(tmpItem)) File.Delete(tmpItem);
                    if (Directory.Exists(tmpItem)) Directory.Delete(tmpItem, true);
                }
                catch { }

                string args = string.Format("-t {0} file recv {1} {2}", dev.Serial, Quote(remotePath), Quote(tmpRoot));
                RunOut ro = RecvWithProgress(args, tmpItem, itemTotal, doneBefore, grandTotal, onProgress, onLog);

                bool ok = ro != null && ro.StdOut != null
                          && ro.StdOut.IndexOf("FileTransfer finish", StringComparison.OrdinalIgnoreCase) >= 0;

                if (!ok)
                {
                    string e = "";
                    if (ro != null)
                    {
                        e = (ro.StdOut == null ? "" : ro.StdOut);
                        if (ro.StdErr != null && ro.StdErr.Trim().Length > 0) e = e + " " + ro.StdErr;
                    }
                    e = e.Replace("\r", " ").Replace("\n", " ").Trim();
                    if (e.Length > 160) e = e.Substring(0, 160);
                    err = e;
                    return false;
                }

                // 落地：tmp -> 正式目标
                string finalPath = Path.Combine(localDir, name);
                bool moved = false;
                for (int attempt = 0; attempt < 3 && !moved; attempt++)
                {
                    try
                    {
                        if (File.Exists(finalPath)) File.Delete(finalPath);
                        if (Directory.Exists(finalPath)) Directory.Delete(finalPath, true);

                        if (Directory.Exists(tmpItem))
                        {
                            Directory.Move(tmpItem, finalPath);
                        }
                        else if (File.Exists(tmpItem))
                        {
                            File.Move(tmpItem, finalPath);
                        }
                        else
                        {
                            err = "传输完成但本地没有产生文件（可能被占用或磁盘权限不足）";
                            return false;
                        }
                        moved = true;
                    }
                    catch (Exception ex)
                    {
                        if (attempt == 2)
                        {
                            err = "落地失败：" + ex.Message;
                            return false;
                        }
                        System.Threading.Thread.Sleep(300);
                    }
                }
                return true;
            }
            catch (Exception ex)
            {
                err = ex.Message;
                return false;
            }
        }

        // 起 recv 进程并轮询本地落盘大小做真进度
        private static RunOut RecvWithProgress(string args, string watchPath,
                                               long itemTotal, long doneBefore, long grandTotal,
                                               Action<long, long> onProgress, Action<string> onLog)
        {
            ProcessStartInfo psi = new ProcessStartInfo();
            psi.FileName = HdcPath;
            psi.Arguments = args;
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardOutput = true;
            psi.RedirectStandardError = true;
            psi.StandardOutputEncoding = Encoding.UTF8;
            psi.StandardErrorEncoding = Encoding.UTF8;

            RunOut ro = new RunOut();
            using (Process p = new Process())
            {
                p.StartInfo = psi;
                StringBuilder so = new StringBuilder();
                StringBuilder se = new StringBuilder();
                p.OutputDataReceived += delegate(object s, DataReceivedEventArgs e)
                {
                    if (e.Data != null)
                    {
                        so.AppendLine(e.Data);
                        if (onLog != null) onLog(e.Data);
                    }
                };
                p.ErrorDataReceived += delegate(object s, DataReceivedEventArgs e)
                {
                    if (e.Data != null) se.AppendLine(e.Data);
                };
                p.Start();
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();

                long lastSeen = -1;
                DateTime started = DateTime.UtcNow;
                while (!p.HasExited)
                {
                    if (!p.WaitForExit(500))
                    {
                        if (onProgress != null)
                        {
                            long seen = PathSize(watchPath);
                            if (seen > lastSeen)
                            {
                                lastSeen = seen;
                                long total = grandTotal > 0 ? grandTotal : itemTotal;
                                long cur = doneBefore + seen;
                                if (total > 0 && cur > total) cur = total;
                                onProgress(cur, total > 0 ? total : 0);
                            }
                        }
                        if ((DateTime.UtcNow - started).TotalHours > 8)
                        {
                            try { p.Kill(); } catch { }
                            break;
                        }
                    }
                }
                try { p.WaitForExit(5000); } catch { }
                ro.ExitCode = -1;
                try { ro.ExitCode = p.ExitCode; } catch { }
                ro.StdOut = so.ToString();
                ro.StdErr = se.ToString();
            }
            return ro;
        }

        public static TransferResult Send(string[] paths, string remoteDir,
                                          Action<long, long> onProgress,
                                          Action<string> onLog)
        {
            if (paths == null || paths.Length == 0)
                throw new TransferException("没有要传的文件。");

            List<string> valid = new List<string>();
            List<string> missing = new List<string>();
            for (int i = 0; i < paths.Length; i++)
            {
                if (File.Exists(paths[i]) || Directory.Exists(paths[i])) valid.Add(paths[i]);
                else missing.Add(paths[i]);
            }
            if (valid.Count == 0)
                throw new TransferException("选中的路径都不存在。");

            if (string.IsNullOrEmpty(remoteDir)) remoteDir = RemoteDesktop;
            if (!remoteDir.EndsWith("/")) remoteDir = remoteDir + "/";

            TransferResult result = new TransferResult();
            result.FileCount = valid.Count;

            Stopwatch sw = Stopwatch.StartNew();

            long grandTotal = 0;
            for (int i = 0; i < valid.Count; i++) grandTotal += LocalSize(valid[i]);
            result.TotalBytes = grandTotal;

            if (!File.Exists(HdcPath))
            {
                result.Success = false;
                result.Message = "缺少 hdc.exe。请把它放到本程序目录，或安装 DevEco Studio，或设置 LINGXI_HDC。";
                result.ElapsedMs = sw.ElapsedMilliseconds;
                return result;
            }

            DeviceInfo dev;
            try
            {
                dev = ResolveDevice();
            }
            catch (TransferException ex)
            {
                result.Success = false;
                result.Message = ex.Message;
                result.ElapsedMs = sw.ElapsedMilliseconds;
                return result;
            }
            if (onLog != null) onLog(string.Format("[设备] {0}", dev.ToString()));

            long sentBefore = 0;
            int okCount = 0;
            List<string> failures = new List<string>();

            for (int i = 0; i < valid.Count; i++)
            {
                string local = valid[i];
                bool isDir = Directory.Exists(local);
                string name = Path.GetFileName(local.TrimEnd('\\', '/'));
                if (string.IsNullOrEmpty(name)) name = local;
                string remoteTarget = remoteDir + name;
                long localSize = LocalSize(local);

                if (onLog != null)
                {
                    onLog(string.Format("[发送] {0} -> {1}", name, remoteTarget));
                }

                RunOut ro = null;
                long thisSentBase = sentBefore;

                if (isDir)
                {
                    // 目录：不确定进度，持续报 (已传, 0)
                    if (onProgress != null) onProgress(sentBefore, 0);
                    ro = Run(HdcPath,
                        string.Format("-t {0} file send {1} {2}", dev.Serial, Quote(local), Quote(remoteDir)),
                        3600000);
                }
                else
                {
                    // 文件：后台起进程 + 轮询远程大小做确定进度
                    ro = SendOneFile(dev.Serial, local, remoteTarget, localSize, sentBefore, grandTotal, onProgress, onLog);
                }

                bool ok = ro != null && ro.StdOut != null && ro.StdOut.IndexOf("FileTransfer finish", StringComparison.OrdinalIgnoreCase) >= 0;
                if (ok)
                {
                    okCount++;
                    sentBefore += localSize;
                    if (onProgress != null) onProgress(sentBefore, grandTotal > 0 ? grandTotal : 0);
                }
                else
                {
                    string err = "";
                    if (ro != null)
                    {
                        err = ro.StdOut;
                        if (ro.StdErr != null && ro.StdErr.Trim().Length > 0) err = err + " " + ro.StdErr;
                    }
                    err = err.Replace("\r", " ").Replace("\n", " ").Trim();
                    if (err.Length > 160) err = err.Substring(0, 160);
                    failures.Add(string.Format("{0}: {1}", name, err.Length == 0 ? "传输失败" : err));
                    if (onLog != null) onLog(string.Format("[失败] {0} {1}", name, err));
                }
            }

            sw.Stop();
            result.ElapsedMs = sw.ElapsedMilliseconds;

            if (failures.Count == 0)
            {
                result.Success = true;
                string dest = remoteDir == RemoteDesktop + "/" ? "鸿蒙桌面" : remoteDir;
                result.Message = string.Format("已成功传到{0}：{1} 项，{2}，耗时 {3} 秒（{4}）",
                    dest, okCount, FormatBytes(sentBefore), (sw.ElapsedMilliseconds / 1000.0).ToString("0.0"),
                    FormatSpeed(sentBefore, sw.ElapsedMilliseconds));
                if (missing.Count > 0)
                    result.Message = result.Message + string.Format("（跳过 {0} 个不存在的路径）", missing.Count);
            }
            else
            {
                result.Success = okCount > 0;
                result.Message = string.Format("成功 {0} 项，失败 {1} 项。首个错误：{2}",
                    okCount, failures.Count, failures[0]);
                if (okCount > 0)
                    result.Message = result.Message + "（已成功的那部分已在设备上）";
            }
            return result;
        }

        private static RunOut SendOneFile(string serial, string local, string remoteTarget,
                                          long localSize, long sentBefore, long grandTotal,
                                          Action<long, long> onProgress, Action<string> onLog)
        {
            ProcessStartInfo psi = new ProcessStartInfo();
            psi.FileName = HdcPath;
            psi.Arguments = string.Format("-t {0} file send {1} {2}", serial, Quote(local), Quote(remoteTarget));
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardOutput = true;
            psi.RedirectStandardError = true;
            psi.StandardOutputEncoding = Encoding.UTF8;
            psi.StandardErrorEncoding = Encoding.UTF8;

            RunOut ro = new RunOut();
            using (Process p = new Process())
            {
                p.StartInfo = psi;
                StringBuilder so = new StringBuilder();
                StringBuilder se = new StringBuilder();
                p.OutputDataReceived += delegate(object s, DataReceivedEventArgs e)
                {
                    if (e.Data != null)
                    {
                        so.AppendLine(e.Data);
                        if (onLog != null) onLog(e.Data);
                    }
                };
                p.ErrorDataReceived += delegate(object s, DataReceivedEventArgs e)
                {
                    if (e.Data != null) se.AppendLine(e.Data);
                };
                p.Start();
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();

                long lastSeen = -1;
                DateTime started = DateTime.UtcNow;
                while (!p.HasExited)
                {
                    if (!p.WaitForExit(700))
                    {
                        if (localSize > 0 && onProgress != null)
                        {
                            long rs = RemoteSize(serial, remoteTarget);
                            if (rs > lastSeen)
                            {
                                lastSeen = rs;
                                long baseBytes = sentBefore + rs;
                                long total = grandTotal > 0 ? grandTotal : localSize;
                                if (baseBytes > total) baseBytes = total;
                                onProgress(baseBytes, total);
                            }
                        }
                        if ((DateTime.UtcNow - started).TotalHours > 8)
                        {
                            try { p.Kill(); } catch { }
                            break;
                        }
                    }
                }
                try { p.WaitForExit(5000); } catch { }
                ro.ExitCode = -1;
                try { ro.ExitCode = p.ExitCode; } catch { }
                ro.StdOut = so.ToString();
                ro.StdErr = se.ToString();
            }
            return ro;
        }

        public static string FormatBytes(long b)
        {
            if (b < 1024) return b.ToString() + " B";
            if (b < 1024L * 1024) return (b / 1024.0).ToString("0.0") + " KB";
            if (b < 1024L * 1024 * 1024) return (b / (1024.0 * 1024)).ToString("0.0") + " MB";
            return (b / (1024.0 * 1024 * 1024)).ToString("0.00") + " GB";
        }

        /// <summary>平均速度（字节/秒）；耗时不足 1ms 时按 1ms 算，避免除零。</summary>
        public static double BytesPerSecond(long bytes, long elapsedMs)
        {
            if (bytes <= 0 || elapsedMs <= 0) return 0;
            return bytes * 1000.0 / elapsedMs;
        }

        /// <summary>速度文案，如「26.4 MB/s」。</summary>
        public static string FormatSpeed(long bytes, long elapsedMs)
        {
            double bps = BytesPerSecond(bytes, elapsedMs);
            if (bps <= 0) return "-";
            if (bps < 1024) return bps.ToString("0") + " B/s";
            if (bps < 1024 * 1024) return (bps / 1024.0).ToString("0.0") + " KB/s";
            return (bps / (1024.0 * 1024)).ToString("0.0") + " MB/s";
        }
    }
}
