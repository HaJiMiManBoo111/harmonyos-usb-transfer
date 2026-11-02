using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace Lingxi
{
    /// <summary>
    /// 灵犀主窗口（纯代码构建，无设计器文件；C# 5 语法）。
    /// 第 1 页「发送到鸿蒙」= v1 界面；第 2 页「从鸿蒙接收」= v2。
    /// 所有跨线程 UI 更新统一走 UiInvoke -> BeginInvoke。
    /// 布局全部手算 SetBounds，不用 Dock / Anchor / TableLayout。
    /// </summary>
    public class MainForm : System.Windows.Forms.Form
    {
        private const string NoDeviceText = "未找到鸿蒙设备，请插数据线并开启 USB 调试";
        private const string DropHint = "把文件或文件夹拖到这里";
        private const string DropHintSub = "支持多选；也可以点下方「添加文件 / 添加文件夹」";

        // ---------------- 公共 ----------------
        private readonly ToolTip _tips = new ToolTip();

        private TabControl _tabs;
        private TabPage _pageSend;
        private TabPage _pageRecv;

        // ---------------- v1：发送页 ----------------
        private readonly List<string> _pending = new List<string>();

        private Label _lblDevice;
        private StatusPill _pillDevice;
        private CardPanel _cardList;
        private CardPanel _cardLog;
        private Panel _panelDrop;
        private ListBox _lstFiles;
        private TextBox _txtLog;
        private FlatProgress _prg;
        private Label _lblSpeed;
        private Label _lblStatus;
        private Button _btnAddFile;
        private Button _btnAddFolder;
        private Button _btnSend;
        private Button _btnClear;
        private FlowLayoutPanel _flowSend;

        private bool _sending;
        private readonly SpeedMeter _speedSend = new SpeedMeter();
        private readonly SpeedMeter _speedRecv = new SpeedMeter();
        private long _speedT0Send;
        private long _speedT0Recv;
        private bool _dragHot;
        private int _tipIndex = -1;
        private GripBar _gripSend;
        private GripBar _gripRecv;
        private float _splitSend = 0.42F;   // 待发送列表占中间区高度比例
        private float _splitRecv = 0.58F;   // 远程列表占中间区高度比例
        private int _midTopSend;            // 发送页中间区顶部（屏幕外坐标）
        private int _midTotalSend;          // 发送页中间区总高
        private int _midTopRecv;
        private int _midTotalRecv;

        // ---------------- v2：接收页 ----------------
        private ComboBox _cmbRemoteDir;
        private Button _btnRefresh;
        private ListView _lvRemote;
        private Button _btnPull;
        private Button _btnPullAll;
        private Button _btnOpenLocal;
        private Label _lblLocalDir;
        private CardPanel _cardRemote;
        private CardPanel _cardRecvLog;

        private TextBox _txtRecvLog;
        private FlatProgress _prgRecv;
        private Label _lblSpeedRecv;
        private Label _lblRecvStatus;
        private FlowLayoutPanel _flowRecv;

        private const int ColKind = 0;
        private const int ColName = 1;
        private const int ColSize = 2;
        private const int SortNone = 0;
        private const int SortAsc = 1;
        private const int SortDesc = 2;

        private int _sortCol = -1;
        private int _sortDir = SortNone;
        private bool _refreshing;
        private bool _pulling;
        private int _pullCount;

        private readonly string _localDir;

        /// <summary>
        /// 设置窗口图标。exe 内嵌图标只影响资源管理器显示，窗体标题栏/任务栏
        /// 走的是 Form.Icon，不设就一直是 WinForms 默认的彩色方块。
        /// 优先读同目录 app.ico，其次读 exe 自身内嵌图标。
        /// </summary>
        private void SetAppIcon()
        {
            try
            {
                string baseDir = Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location);
                string icoPath = Path.Combine(baseDir, "app.ico");
                if (File.Exists(icoPath))
                {
                    this.Icon = new Icon(icoPath);
                    return;
                }
                this.Icon = Icon.ExtractAssociatedIcon(System.Reflection.Assembly.GetExecutingAssembly().Location);
            }
            catch (Exception)
            {
                // 图标拿不到不影响功能，忽略
            }
        }

        // ================================================================ 构造

        public MainForm()
        {
            this.SuspendLayout();

            this.Text = "灵犀";
            this.AutoScaleMode = AutoScaleMode.None;
            this.SetAppIcon();
            this.Font = Fluent.Body;
            this.BackColor = Fluent.WindowBg;
            this.ClientSize = new Size(Fluent.S(760), Fluent.S(580));
            this.MinimumSize = new Size(Fluent.S(680), Fluent.S(520));
            this.StartPosition = FormStartPosition.CenterScreen;
            this.FormBorderStyle = FormBorderStyle.Sizable;
            this.MaximizeBox = true;
            this.AllowDrop = true;                  // 整个窗口兜底接收拖拽

            _localDir = HdcCore.DefaultPullDir();

            this.BuildSendPage();
            this.BuildRecvPage();

            _tabs = new FluentTabs();
            _tabs.Name = "tabs";
            _tabs.Location = new Point(0, 0);
            _tabs.Size = new Size(Fluent.S(760), Fluent.S(580));
            _tabs.TabPages.Add(_pageSend);
            _tabs.TabPages.Add(_pageRecv);
            this.Controls.Add(_tabs);

            this.Load += new EventHandler(this.MainForm_Load);
            this.Resize += new EventHandler(this.MainForm_Resize);

            this.ResumeLayout(false);
            this.PerformLayout();

            this.LayoutChildren();
        }

        // ---------------------------------------------------------------- 发送页构建

        private void BuildSendPage()
        {
            _pageSend = new TabPage();
            _pageSend.Name = "pageSend";
            _pageSend.Text = "发送到鸿蒙";
            _pageSend.BackColor = Fluent.WindowBg;
            _pageSend.AllowDrop = true;
            _pageSend.DragEnter += new DragEventHandler(this.DropArea_DragEnter);
            _pageSend.DragOver += new DragEventHandler(this.DropArea_DragEnter);
            _pageSend.DragLeave += new EventHandler(this.DropArea_DragLeave);
            _pageSend.DragDrop += new DragEventHandler(this.DropArea_DragDrop);

            // 1) 顶部：设备状态胶囊（左，全窗口唯一高饱和色）+ 应用名（右）
            _pillDevice = new StatusPill();
            _pillDevice.Name = "lblDevice";
            _pillDevice.SetPending("正在检测设备…");

            _lblDevice = new Label();
            _lblDevice.Name = "lblDeviceText";
            _lblDevice.Text = "传送到 · 鸿蒙桌面";
            _lblDevice.Font = Fluent.Body;
            _lblDevice.TextAlign = ContentAlignment.MiddleRight;
            _lblDevice.AutoEllipsis = true;

            // 2) 拖拽区（圆角虚线框由 Paint 自绘；不放子控件，避免抢走 OLE 拖放目标）
            _panelDrop = new Panel();
            _panelDrop.Name = "panelDrop";
            _panelDrop.BackColor = Fluent.WindowBg;
            _panelDrop.AllowDrop = true;
            _panelDrop.Paint += new PaintEventHandler(this.DropArea_Paint);
            _panelDrop.DragEnter += new DragEventHandler(this.DropArea_DragEnter);
            _panelDrop.DragOver += new DragEventHandler(this.DropArea_DragEnter);
            _panelDrop.DragLeave += new EventHandler(this.DropArea_DragLeave);
            _panelDrop.DragDrop += new DragEventHandler(this.DropArea_DragDrop);

            // 3) 待发送列表（白卡片 + 左上小节标题）
            _lstFiles = new ListBox();
            _lstFiles.Name = "lstFiles";
            _lstFiles.SelectionMode = SelectionMode.MultiExtended;
            _lstFiles.HorizontalScrollbar = true;
            _lstFiles.IntegralHeight = false;
            _lstFiles.BorderStyle = BorderStyle.None;
            _lstFiles.BackColor = Fluent.CardBg;
            _lstFiles.ForeColor = Fluent.TextPrimary;
            _lstFiles.Font = Fluent.Body;
            _lstFiles.MouseMove += new MouseEventHandler(this.LstFiles_MouseMove);

            _cardList = new CardPanel();
            _cardList.Caption = "待发送";
            _cardList.Controls.Add(_lstFiles);

            // 4) 日志（同款卡片；等宽字体、次级灰，弱化不抢视线）
            _txtLog = new TextBox();
            _txtLog.Name = "txtLog";
            _txtLog.Multiline = true;
            _txtLog.ReadOnly = true;
            _txtLog.WordWrap = false;
            _txtLog.ScrollBars = ScrollBars.Both;
            _txtLog.BorderStyle = BorderStyle.None;
            _txtLog.BackColor = Fluent.CardBg;
            _txtLog.ForeColor = Fluent.TextTertiary;
            _txtLog.Font = Fluent.Mono;

            _cardLog = new CardPanel();
            _cardLog.Caption = "传输日志";
            _cardLog.Controls.Add(_txtLog);

            // 5) 底部：进度条 + 状态行 + 按钮组
            _prg = new FlatProgress();
            _prg.Name = "prg";
            _prg.Minimum = 0;
            _prg.Maximum = 100;
            _prg.Value = 0;
            _prg.Style = ProgressBarStyle.Continuous;

            _lblSpeed = new Label();
            _lblSpeed.Name = "lblSpeed";
            _lblSpeed.Text = "";
            _lblSpeed.TextAlign = ContentAlignment.MiddleRight;
            _lblSpeed.AutoEllipsis = true;
            _lblSpeed.Font = Fluent.BodyStrong;
            _lblSpeed.ForeColor = Fluent.Accent;

            _lblStatus = new Label();
            _lblStatus.Name = "lblStatus";
            _lblStatus.Text = "就绪。拖入文件后点「发送到鸿蒙桌面」。";
            _lblStatus.TextAlign = ContentAlignment.MiddleLeft;
            _lblStatus.AutoEllipsis = true;
            _lblStatus.Font = Fluent.Body;
            _lblStatus.ForeColor = Fluent.TextSecondary;

            _btnAddFile = MakeButton("btnAddFile", "添加文件", 92, FluentButtonKind.Secondary);
            _btnAddFolder = MakeButton("btnAddFolder", "添加文件夹", 106, FluentButtonKind.Secondary);
            _btnSend = MakeButton("btnSend", "发送到鸿蒙桌面", 148, FluentButtonKind.Primary);
            _btnClear = MakeButton("btnClear", "清空", 68, FluentButtonKind.Subtle);

            _btnAddFile.Click += new EventHandler(this.BtnAddFile_Click);
            _btnAddFolder.Click += new EventHandler(this.BtnAddFolder_Click);
            _btnSend.Click += new EventHandler(this.BtnSend_Click);
            _btnClear.Click += new EventHandler(this.BtnClear_Click);

            _flowSend = new FlowLayoutPanel();
            _flowSend.Name = "flowSend";
            _flowSend.WrapContents = false;
            _flowSend.FlowDirection = FlowDirection.LeftToRight;
            _flowSend.AutoScroll = false;
            _flowSend.Margin = new Padding(0);
            _flowSend.Padding = new Padding(0);
            _flowSend.BackColor = Fluent.WindowBg;
            _flowSend.Controls.Add(_btnAddFile);
            _flowSend.Controls.Add(_btnAddFolder);
            _flowSend.Controls.Add(_btnSend);
            _flowSend.Controls.Add(_btnClear);

            _gripSend = new GripBar();
            _gripSend.Name = "gripSend";
            _gripSend.Moved += new EventHandler(this.GripSend_Moved);

            _pageSend.Controls.AddRange(new Control[] {
                _lblSpeed,
                _pillDevice, _lblDevice, _panelDrop, _cardList, _gripSend, _cardLog,
                _prg, _lblStatus, _flowSend
            });
        }

        /// <summary>造一个 Fluent 按钮（Name / 文案 / 宽度 / 语义）。</summary>
        private static FluentButton MakeButton(string name, string text, int width, FluentButtonKind kind)
        {
            FluentButton b = new FluentButton();
            b.Name = name;
            b.Text = text;
            b.Width = Fluent.S(width);
            b.Height = Fluent.S(30);
            b.Kind = kind;
            b.Margin = new Padding(0, 0, Fluent.S(8), 0);
            return b;
        }

        // ---------------------------------------------------------------- 接收页构建

        private void BuildRecvPage()
        {
            _pageRecv = new TabPage();
            _pageRecv.Name = "pageRecv";
            _pageRecv.Text = "从鸿蒙接收";
            _pageRecv.BackColor = Fluent.WindowBg;

            // 顶部：远程目录下拉 + 刷新
            _cmbRemoteDir = new ComboBox();
            _cmbRemoteDir.Name = "cmbRemoteDir";
            _cmbRemoteDir.DropDownStyle = ComboBoxStyle.DropDownList;
            _cmbRemoteDir.Font = Fluent.Body;
            _cmbRemoteDir.BackColor = Fluent.CardBg;
            _cmbRemoteDir.ForeColor = Fluent.TextPrimary;
            _cmbRemoteDir.Items.AddRange(HdcCore.RemoteRoots);
            if (_cmbRemoteDir.Items.Count > 0)
            {
                _cmbRemoteDir.SelectedIndex = 0;   // 默认 Desktop
            }
            _cmbRemoteDir.SelectedIndexChanged += new EventHandler(this.CmbRemoteDir_Changed);

            _btnRefresh = MakeButton("btnRefresh", "刷新列表", 100, FluentButtonKind.Secondary);
            _btnRefresh.Height = Fluent.S(26);
            _btnRefresh.Click += new EventHandler(this.BtnRefresh_Click);

            // 本地落盘位置
            _lblLocalDir = new Label();
            _lblLocalDir.Name = "lblLocalDir";
            _lblLocalDir.Font = Fluent.HintSub;
            _lblLocalDir.ForeColor = Fluent.TextTertiary;
            _lblLocalDir.TextAlign = ContentAlignment.MiddleLeft;
            _lblLocalDir.AutoEllipsis = true;
            _lblLocalDir.Text = string.Format("接收位置：{0}", _localDir);
            _tips.SetToolTip(_lblLocalDir, _localDir);

            // 中间：远程文件列表（卡片）
            _lvRemote = new ListView();
            _lvRemote.Name = "lvRemote";
            _lvRemote.View = View.Details;
            _lvRemote.FullRowSelect = true;
            _lvRemote.MultiSelect = true;
            _lvRemote.HideSelection = false;
            _lvRemote.GridLines = false;
            _lvRemote.Sorting = SortOrder.None;
            _lvRemote.BorderStyle = BorderStyle.None;
            _lvRemote.BackColor = Fluent.CardBg;
            _lvRemote.ForeColor = Fluent.TextPrimary;
            _lvRemote.Font = Fluent.Body;
            _lvRemote.Columns.Add("类型", Fluent.S(58), HorizontalAlignment.Left);
            _lvRemote.Columns.Add("名称", Fluent.S(540), HorizontalAlignment.Left);
            _lvRemote.Columns.Add("大小", Fluent.S(100), HorizontalAlignment.Right);
            _lvRemote.ColumnClick += new ColumnClickEventHandler(this.LvRemote_ColumnClick);
            _lvRemote.DoubleClick += new EventHandler(this.LvRemote_DoubleClick);

            _cardRemote = new CardPanel();
            _cardRemote.Caption = "远程文件";
            _cardRemote.Controls.Add(_lvRemote);

            // 底部日志 + 状态 + 按钮
            _txtRecvLog = new TextBox();
            _txtRecvLog.Name = "txtRecvLog";
            _txtRecvLog.Multiline = true;
            _txtRecvLog.ReadOnly = true;
            _txtRecvLog.WordWrap = false;
            _txtRecvLog.ScrollBars = ScrollBars.Both;
            _txtRecvLog.BorderStyle = BorderStyle.None;
            _txtRecvLog.BackColor = Fluent.CardBg;
            _txtRecvLog.ForeColor = Fluent.TextTertiary;
            _txtRecvLog.Font = Fluent.Mono;

            _cardRecvLog = new CardPanel();
            _cardRecvLog.Caption = "接收日志";
            _cardRecvLog.Controls.Add(_txtRecvLog);

            _prgRecv = new FlatProgress();
            _prgRecv.Name = "prgRecv";
            _prgRecv.Minimum = 0;
            _prgRecv.Maximum = 100;
            _prgRecv.Value = 0;
            _prgRecv.Style = ProgressBarStyle.Continuous;

            _lblSpeedRecv = new Label();
            _lblSpeedRecv.Name = "lblSpeedRecv";
            _lblSpeedRecv.Text = "";
            _lblSpeedRecv.TextAlign = ContentAlignment.MiddleRight;
            _lblSpeedRecv.AutoEllipsis = true;
            _lblSpeedRecv.Font = Fluent.BodyStrong;
            _lblSpeedRecv.ForeColor = Fluent.Accent;

            _lblRecvStatus = new Label();
            _lblRecvStatus.Name = "lblRecvStatus";
            _lblRecvStatus.TextAlign = ContentAlignment.MiddleLeft;
            _lblRecvStatus.AutoEllipsis = true;
            _lblRecvStatus.Font = Fluent.Body;
            _lblRecvStatus.ForeColor = Fluent.TextSecondary;
            _lblRecvStatus.Text = "先选远程目录，再点「刷新列表」。";

            _btnPull = MakeButton("btnPull", "接收选中", 108, FluentButtonKind.Primary);
            _btnPull.Click += new EventHandler(this.BtnPull_Click);

            _btnPullAll = MakeButton("btnPullAll", "全部接收", 100, FluentButtonKind.Secondary);
            _btnPullAll.Click += new EventHandler(this.BtnPullAll_Click);

            _btnOpenLocal = MakeButton("btnOpenLocal", "打开接收文件夹", 132, FluentButtonKind.Subtle);
            _btnOpenLocal.Click += new EventHandler(this.BtnOpenLocal_Click);

            _flowRecv = new FlowLayoutPanel();
            _flowRecv.Name = "flowRecv";
            _flowRecv.WrapContents = false;
            _flowRecv.FlowDirection = FlowDirection.LeftToRight;
            _flowRecv.AutoScroll = false;
            _flowRecv.Margin = new Padding(0);
            _flowRecv.Padding = new Padding(0);
            _flowRecv.BackColor = Fluent.WindowBg;
            _flowRecv.Controls.Add(_btnPull);
            _flowRecv.Controls.Add(_btnPullAll);
            _flowRecv.Controls.Add(_btnOpenLocal);

            _gripRecv = new GripBar();
            _gripRecv.Name = "gripRecv";
            _gripRecv.Moved += new EventHandler(this.GripRecv_Moved);

            _pageRecv.Controls.AddRange(new Control[] {
                _lblSpeedRecv,
                _cmbRemoteDir, _btnRefresh, _lblLocalDir, _cardRemote, _gripRecv, _cardRecvLog,
                _prgRecv, _lblRecvStatus, _flowRecv
            });
        }

        // ================================================================ 布局

        private void MainForm_Resize(object sender, EventArgs e)
        {
            this.LayoutChildren();
        }

        private void LayoutChildren()
        {
            if (_tabs == null || _pageSend == null || _pageRecv == null)
            {
                return;
            }
            int w = this.ClientSize.Width;
            int h = this.ClientSize.Height;
            if (w < Fluent.S(100) || h < Fluent.S(100))
            {
                return;
            }

            _tabs.SetBounds(0, 0, w, h);

            this.LayoutSendPage(PageArea(_pageSend, w, h));
            this.LayoutRecvPage(PageArea(_pageRecv, w, h));
        }

        /// <summary>
        /// 取某个标签页的可用客户区（页内坐标，从 0,0 起）。
        /// 不能用 page.ClientRectangle：TabControl 在 Resize 事件里还没把新尺寸同步给 TabPage，
        /// 读到的是上一帧的值（窗口缩小时控件会溢出到右边界外）。
        /// 这里按 TabControl 的边框与标签条高度直接算，任何时候都跟当前窗口尺寸一致。
        /// </summary>
        private Rectangle PageArea(TabPage page, int w, int h)
        {
            if (page == null)
            {
                return new Rectangle(0, 0, w, h);
            }

            int border = Fluent.S(4);
            int strip = FluentTabs.StripHeightPx;
            if (_tabs != null && _tabs.ItemSize.Height > 0)
            {
                strip = _tabs.ItemSize.Height + border + Fluent.S(6);
            }

            int pw = w - border * 2;
            int ph = h - strip - border;
            if (pw < 200)
            {
                pw = 200;
            }
            if (ph < 200)
            {
                ph = 200;
            }
            return new Rectangle(0, 0, pw, ph);
        }

        /// <summary>
        /// 发送页：顶部设备行 / 拖拽区固定高，中间列表与日志分摊多余高度，
        /// 底部进度条-状态-按钮贴底。
        /// </summary>
        private void LayoutSendPage(Rectangle area)
        {
            if (_pillDevice == null || _lblDevice == null || _panelDrop == null
                || _cardList == null || _cardLog == null || _lstFiles == null
                || _txtLog == null || _prg == null || _lblStatus == null || _lblSpeed == null
                || _flowSend == null)
            {
                return;
            }

            int w = area.Width;
            int h = area.Height;
            if (w < 100 || h < 100)
            {
                return;
            }

            int ox = area.X;
            int oy = area.Y;

            int pad = Fluent.S(16);
            int innerW = Math.Max(Fluent.S(160), w - pad * 2);

            // 顶部行：状态胶囊 + 标题
            int pillH = Fluent.S(28);
            int pillW = Math.Min(Fluent.S(430), Math.Max(Fluent.S(200), innerW * 62 / 100));
            int titleW = Math.Max(Fluent.S(80), innerW - pillW - Fluent.S(12));
            _pillDevice.SetBounds(ox + pad, oy + Fluent.S(14), pillW, pillH);
            _lblDevice.SetBounds(ox + pad + innerW - titleW, oy + Fluent.S(14), titleW, pillH);

            // 拖拽区
            int dropTop = oy + Fluent.S(14) + pillH + Fluent.S(12);
            int dropH = Fluent.S(96);
            _panelDrop.SetBounds(ox + pad, dropTop, innerW, dropH);

            // 底部固定区
            int flowH = Fluent.S(34);
            int flowY = h - pad - flowH;
            int statusY = flowY - Fluent.S(26);
            int prgY = statusY - Fluent.S(14);

            // 中间区：待发送列表 / 分隔条 / 日志，高度由 _splitSend 决定（可拖动）
            int gripH = Fluent.S(12);
            int midTop = dropTop + dropH + Fluent.S(14);
            int midBottom = oy + prgY - Fluent.S(14);
            int total = midBottom - midTop;
            if (total < 80)
            {
                total = 80;
            }
            int listH;
            int logH;
            _midTopSend = midTop;
            _midTotalSend = total;
            SplitHeights(total, gripH, _splitSend, Fluent.S(26), Fluent.S(40), out listH, out logH);

            _cardList.SetBounds(ox + pad, midTop, innerW, listH);
            LayoutCardChild(_lstFiles, _cardList, _cardList.CaptionHeight);
            _gripSend.SetBounds(ox + pad, midTop + listH, innerW, gripH);
            _cardLog.SetBounds(ox + pad, midTop + listH + gripH, innerW, logH);
            LayoutCardChild(_txtLog, _cardLog, _cardLog.CaptionHeight);

            _prg.SetBounds(ox + pad, oy + prgY, innerW, Fluent.S(8));
            int speedW = Fluent.S(150);
            _lblStatus.SetBounds(ox + pad, oy + statusY, innerW - speedW - Fluent.S(8), Fluent.S(22));
            _lblSpeed.SetBounds(ox + pad + innerW - speedW, oy + statusY, speedW, Fluent.S(22));
            _flowSend.SetBounds(ox + pad, oy + flowY, innerW, flowH);
        }

        /// <summary>把卡片内的子控件按 12px 内边距 + 顶部标题高度摆放。</summary>
        private static void LayoutCardChild(Control child, CardPanel card, int headerH)
        {
            if (child == null || card == null)
            {
                return;
            }
            int cw = card.Width - Fluent.S(24);
            int ch = card.Height - headerH - Fluent.S(12);
            if (cw < Fluent.S(10))
            {
                cw = Fluent.S(10);
            }
            if (ch < Fluent.S(10))
            {
                ch = Fluent.S(10);
            }
            child.SetBounds(Fluent.S(12), headerH, cw, ch);
        }

        /// <summary>
        /// 接收页：下拉框 + 刷新按钮一行，接收位置一行，中间列表，下部日志 + 进度 + 状态 + 按钮。
        /// </summary>
        private void LayoutRecvPage(Rectangle area)
        {
            if (_cmbRemoteDir == null || _btnRefresh == null || _lblLocalDir == null
                || _cardRemote == null || _cardRecvLog == null || _lvRemote == null
                || _txtRecvLog == null || _prgRecv == null || _lblRecvStatus == null
                || _lblSpeedRecv == null
                || _flowRecv == null)
            {
                return;
            }

            int w = area.Width;
            int h = area.Height;
            if (w < 100 || h < 100)
            {
                return;
            }

            int ox = area.X;
            int oy = area.Y;

            int pad = Fluent.S(16);
            int innerW = Math.Max(Fluent.S(160), w - pad * 2);

            int refreshW = Fluent.S(100);
            int gap = Fluent.S(8);
            int comboH = Fluent.S(26);

            _cmbRemoteDir.SetBounds(ox + pad, oy + Fluent.S(14), Math.Max(Fluent.S(80), innerW - refreshW - gap), comboH);
            _btnRefresh.SetBounds(ox + pad + innerW - refreshW, oy + Fluent.S(14), refreshW, comboH);
            _lblLocalDir.SetBounds(ox + pad, oy + Fluent.S(14) + comboH + Fluent.S(8), innerW, Fluent.S(18));

            int flowH = Fluent.S(34);
            int flowY = h - pad - flowH;
            int statusY = flowY - Fluent.S(26);
            int prgY = statusY - Fluent.S(14);

            int midTop = oy + Fluent.S(14) + comboH + Fluent.S(8) + Fluent.S(18) + Fluent.S(10);
            int midBottom = oy + prgY - Fluent.S(14);
            int total = midBottom - midTop;
            if (total < Fluent.S(90))
            {
                total = Fluent.S(90);
            }
            int gripH = Fluent.S(12);
            _midTopRecv = midTop;
            _midTotalRecv = total;
            int listH;
            int logH;
            SplitHeights(total, gripH, _splitRecv, Fluent.S(26), Fluent.S(40), out listH, out logH);

            _cardRemote.SetBounds(ox + pad, midTop, innerW, listH);
            LayoutCardChild(_lvRemote, _cardRemote, _cardRemote.CaptionHeight);
            _gripRecv.SetBounds(ox + pad, midTop + listH, innerW, gripH);
            _cardRecvLog.SetBounds(ox + pad, midTop + listH + gripH, innerW, logH);
            LayoutCardChild(_txtRecvLog, _cardRecvLog, _cardRecvLog.CaptionHeight);

            _prgRecv.SetBounds(ox + pad, oy + prgY, innerW, Fluent.S(8));
            int speedW2 = Fluent.S(150);
            _lblRecvStatus.SetBounds(ox + pad, oy + statusY, innerW - speedW2 - Fluent.S(8), Fluent.S(22));
            _lblSpeedRecv.SetBounds(ox + pad + innerW - speedW2, oy + statusY, speedW2, Fluent.S(22));
            _flowRecv.SetBounds(ox + pad, oy + flowY, innerW, flowH);
        }

        /// <summary>
        /// 把中间区总高按比例拆成「列表」与「日志」两块（中间留 gripH 给拖动条）。
        /// </summary>
        private static void SplitHeights(int total, int gripH, float ratio,
            int minList, int minLog, out int listH, out int logH)
        {
            int avail = total - gripH;
            if (avail < minList + minLog)
            {
                avail = minList + minLog;
            }

            if (ratio < 0.15F)
            {
                ratio = 0.15F;
            }
            if (ratio > 0.85F)
            {
                ratio = 0.85F;
            }

            listH = (int)(avail * ratio);
            if (listH < minList)
            {
                listH = minList;
            }
            logH = avail - listH;
            if (logH < minLog)
            {
                logH = minLog;
                listH = avail - logH;
            }
        }

        /// <summary>拖动分隔条：按鼠标当前位置反算「列表」占比。</summary>
        private void GripSend_Moved(object sender, EventArgs e)
        {
            float r = _splitSend;
            if (ApplyGripDrag(_gripSend, ref r, _midTopSend, _midTotalSend, Fluent.S(26), Fluent.S(40)))
            {
                _splitSend = r;
                this.LayoutChildren();
            }
        }

        private void GripRecv_Moved(object sender, EventArgs e)
        {
            float r = _splitRecv;
            if (ApplyGripDrag(_gripRecv, ref r, _midTopRecv, _midTotalRecv, Fluent.S(26), Fluent.S(40)))
            {
                _splitRecv = r;
                this.LayoutChildren();
            }
        }

        /// <summary>根据鼠标 Y 位置反算列表高度比例，返回比例是否真的变了。</summary>
        private static bool ApplyGripDrag(GripBar grip, ref float ratio,
            int midTop, int total, int minList, int minLog)
        {
            if (grip == null || grip.Parent == null || total <= 0)
            {
                return false;
            }
            int gripH = grip.Height;
            int avail = total - gripH;
            if (avail < minList + minLog)
            {
                return false;
            }

            int ptY = grip.Parent.PointToClient(Cursor.Position).Y;
            int wantList = ptY - midTop - gripH / 2;
            if (wantList < minList)
            {
                wantList = minList;
            }
            int maxList = avail - minLog;
            if (wantList > maxList)
            {
                wantList = maxList;
            }

            float r = (float)wantList / (float)avail;
            if (r < 0.15F)
            {
                r = 0.15F;
            }
            if (r > 0.85F)
            {
                r = 0.85F;
            }
            if (Math.Abs(r - ratio) < 0.004F)
            {
                return false;
            }
            ratio = r;
            return true;
        }

        // ================================================================ 生命周期

        private void MainForm_Load(object sender, EventArgs e)
        {
            // 探测设备可能在外部进程上花几百毫秒，放后台线程，避免窗体卡住
            Thread t = new Thread(delegate() { this.DetectDevice(); });
            t.IsBackground = true;
            t.Start();
        }

        private void DetectDevice()
        {
            string text;
            bool ok = false;
            try
            {
                DeviceInfo d = HdcCore.ResolveDevice();
                if (d == null)
                {
                    text = NoDeviceText;
                }
                else
                {
                    text = "设备：" + d.ToString();
                    ok = true;
                }
            }
            catch (TransferException)
            {
                text = NoDeviceText;
            }
            catch (Exception)
            {
                text = NoDeviceText;
            }

            this.UiInvoke(delegate()
            {
                _pillDevice.SetState(ok, text);
                _tips.SetToolTip(_pillDevice, text);
            });
        }

        // ================================================================ 跨线程辅助

        /// <summary>
        /// 后台线程调用；需要时切回 UI 线程。
        /// 窗口已经关掉/句柄还没建时静默丢弃（传输出错或用户提前关窗都不该让后台线程抛出去崩进程）。
        /// </summary>
        private void UiInvoke(Action action)
        {
            if (action == null)
            {
                return;
            }
            try
            {
                if (this.IsDisposed || this.Disposing)
                {
                    return;
                }
                if (this.InvokeRequired || !this.IsHandleCreated)
                {
                    this.BeginInvoke(action);
                    return;
                }
                action();
            }
            catch (ObjectDisposedException)
            {
                // 关窗过程中，忽略
            }
            catch (InvalidOperationException)
            {
                // 句柄已销毁 / 正在销毁，忽略
            }
        }

        // ---------------------------------------------------------------- 发送页：状态与日志

        private void SetStatus(string text)
        {
            if (text == null)
            {
                text = string.Empty;
            }
            _lblStatus.Text = text;
            _lblStatus.ForeColor = SystemColors.ControlText;
            _tips.SetToolTip(_lblStatus, text);
        }

        private void AppendLog(string line)
        {
            if (line == null)
            {
                return;
            }
            if (_txtLog.TextLength > 400000)
            {
                _txtLog.Clear();
                _txtLog.AppendText("…(日志过长，已截断)…" + Environment.NewLine);
            }
            _txtLog.AppendText(line + Environment.NewLine);
            _txtLog.SelectionStart = _txtLog.TextLength;
            _txtLog.SelectionLength = 0;
            _txtLog.ScrollToCaret();
        }

        // ---------------------------------------------------------------- 接收页：状态与日志

        private void SetRecvStatus(string text, bool ok)
        {
            if (text == null)
            {
                text = string.Empty;
            }
            _lblRecvStatus.Text = text;
            _lblRecvStatus.ForeColor = ok ? Color.FromArgb(0, 100, 0) : SystemColors.ControlText;
            _tips.SetToolTip(_lblRecvStatus, text);
        }

        private void SetRecvError(string text)
        {
            if (text == null)
            {
                text = string.Empty;
            }
            _lblRecvStatus.Text = text;
            _lblRecvStatus.ForeColor = Color.Firebrick;
            _tips.SetToolTip(_lblRecvStatus, text);
        }

        private void AppendRecvLog(string line)
        {
            if (line == null)
            {
                return;
            }
            if (_txtRecvLog.TextLength > 400000)
            {
                _txtRecvLog.Clear();
                _txtRecvLog.AppendText("…(日志过长，已截断)…" + Environment.NewLine);
            }
            _txtRecvLog.AppendText(line + Environment.NewLine);
            _txtRecvLog.SelectionStart = _txtRecvLog.TextLength;
            _txtRecvLog.SelectionLength = 0;
            _txtRecvLog.ScrollToCaret();
        }

        // ================================================================ 拖拽（发送页）

        private bool IsSendPageActive()
        {
            return _tabs == null || _tabs.SelectedIndex <= 0;
        }

        private void DropArea_DragEnter(object sender, DragEventArgs e)
        {
            if (!this.IsSendPageActive())
            {
                e.Effect = DragDropEffects.None;
                return;
            }
            if (e.Data != null && e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                e.Effect = DragDropEffects.Copy;
                if (!_dragHot)
                {
                    _dragHot = true;
                    _panelDrop.BackColor = Color.FromArgb(226, 240, 255);
                    _panelDrop.Invalidate();
                }
            }
            else
            {
                e.Effect = DragDropEffects.None;
            }
        }

        private void DropArea_DragLeave(object sender, EventArgs e)
        {
            _dragHot = false;
            _panelDrop.BackColor = Color.FromArgb(248, 248, 248);
            _panelDrop.Invalidate();
        }

        private void DropArea_DragDrop(object sender, DragEventArgs e)
        {
            _dragHot = false;
            _panelDrop.BackColor = Color.FromArgb(248, 248, 248);
            _panelDrop.Invalidate();

            if (!this.IsSendPageActive())
            {
                return;
            }
            if (e.Data == null || !e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                return;
            }
            string[] files = e.Data.GetData(DataFormats.FileDrop) as string[];
            if (files == null || files.Length == 0)
            {
                return;
            }
            this.AddPaths(files);
        }

        private void DropArea_Paint(object sender, PaintEventArgs e)
        {
            Panel p = (Panel)sender;
            if (p.Width < 8 || p.Height < 8)
            {
                return;
            }

            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            int edge = Fluent.S(1);
            Rectangle r = new Rectangle(edge, edge, p.Width - edge * 2 - 1, p.Height - edge * 2 - 1);
            using (GraphicsPath path = Fluent.RoundRect(r, Fluent.S(8)))
            {
                using (SolidBrush b = new SolidBrush(_dragHot ? Fluent.AccentSoft : Fluent.CardBg))
                {
                    g.FillPath(b, path);
                }
                using (Pen pen = new Pen(_dragHot ? Fluent.Accent : Fluent.BorderStrong, Fluent.SF(1.4F)))
                {
                    pen.DashStyle = DashStyle.Dash;
                    pen.DashPattern = new float[] { 4.5F, 3F };
                    g.DrawPath(pen, path);
                }
            }

            bool compact = p.Height < Fluent.S(62);
            string main = DropHint;
            string sub = compact ? string.Empty : DropHintSub;

            if (compact)
            {
                Rectangle tr = new Rectangle(0, 0, p.Width, p.Height - 2);
                TextRenderer.DrawText(g, main, Fluent.Hint, tr,
                    _dragHot ? Fluent.Accent : Fluent.TextPrimary,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter
                    | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
            }
            else
            {
                Rectangle tr = new Rectangle(0, 0, p.Width, p.Height - Fluent.S(26));
                TextRenderer.DrawText(g, main, Fluent.Hint, tr,
                    _dragHot ? Fluent.Accent : Fluent.TextPrimary,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter
                    | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
                Rectangle sr = new Rectangle(0, p.Height - Fluent.S(30), p.Width, Fluent.S(20));
                TextRenderer.DrawText(g, sub, Fluent.HintSub, sr, Fluent.TextTertiary,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter
                    | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
            }
        }

        // ================================================================ 发送页：待传列表

        private void LstFiles_MouseMove(object sender, MouseEventArgs e)
        {
            int idx = _lstFiles.IndexFromPoint(e.Location);
            if (idx == _tipIndex)
            {
                return;
            }
            _tipIndex = idx;
            if (idx < 0 || idx >= _pending.Count)
            {
                _tips.SetToolTip(_lstFiles, string.Empty);
                return;
            }
            _tips.SetToolTip(_lstFiles, BuildItemTip(_pending[idx]));
        }

        private static string BuildItemTip(string path)
        {
            string name = Path.GetFileName(path);
            if (name == null || name.Length == 0)
            {
                name = Path.GetFileName(path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            }
            string kind;
            string extra = string.Empty;
            try
            {
                if (Directory.Exists(path))
                {
                    kind = "文件夹";
                }
                else
                {
                    kind = "文件";
                    FileInfo fi = new FileInfo(path);
                    if (fi.Exists)
                    {
                        extra = "，大小 " + FormatSize(fi.Length);
                    }
                }
            }
            catch (Exception)
            {
                kind = "路径";
            }
            return string.Format("{0}（{1}{2}）{3}{4}", name, kind, extra, Environment.NewLine, path);
        }

        private static string FormatSize(long bytes)
        {
            if (bytes < 1024L)
            {
                return string.Format("{0} B", bytes);
            }
            double kb = (double)bytes / 1024.0;
            if (kb < 1024.0)
            {
                return string.Format("{0:F1} KB", kb);
            }
            double mb = kb / 1024.0;
            if (mb < 1024.0)
            {
                return string.Format("{0:F1} MB", mb);
            }
            return string.Format("{0:F2} GB", mb / 1024.0);
        }

        /// <summary>加入待传列表，返回真正新增的条数。必须在 UI 线程调用。</summary>
        private int AddPaths(string[] paths)
        {
            int added = 0;
            int missing = 0;
            for (int i = 0; i < paths.Length; i++)
            {
                string p = paths[i];
                if (p == null || p.Length == 0)
                {
                    continue;
                }
                bool exists = Directory.Exists(p) || File.Exists(p);
                if (!exists)
                {
                    missing++;
                    continue;
                }
                bool dup = false;
                for (int k = 0; k < _pending.Count; k++)
                {
                    if (string.Equals(_pending[k], p, StringComparison.OrdinalIgnoreCase))
                    {
                        dup = true;
                        break;
                    }
                }
                if (dup)
                {
                    continue;
                }
                _pending.Add(p);
                _lstFiles.Items.Add(p);   // 显示完整路径，最不容易看错
                added++;
            }

            if (added > 0)
            {
                SetStatus(string.Format("待传 {0} 项（本次新增 {1} 项）。", _pending.Count, added));
                AppendLog(string.Format("[UI] 加入 {0} 项，待传共 {1} 项。", added, _pending.Count));
            }
            else if (missing > 0)
            {
                SetStatus(string.Format("{0} 个路径不存在，未加入。", missing));
            }
            else if (paths.Length > 0)
            {
                SetStatus("这些项已经在列表里了。");
            }
            return added;
        }

        // ================================================================ 发送页：按钮

        private void BtnAddFile_Click(object sender, EventArgs e)
        {
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                dlg.Title = "选择要发送到鸿蒙桌面的文件";
                dlg.Multiselect = true;
                dlg.CheckFileExists = true;
                dlg.Filter = "所有文件 (*.*)|*.*";
                if (dlg.ShowDialog(this) == DialogResult.OK)
                {
                    this.AddPaths(dlg.FileNames);
                }
            }
        }

        private void BtnAddFolder_Click(object sender, EventArgs e)
        {
            using (FolderBrowserDialog dlg = new FolderBrowserDialog())
            {
                dlg.Description = "选择要发送到鸿蒙桌面的文件夹（会整目录传过去）";
                dlg.ShowNewFolderButton = false;
                if (dlg.ShowDialog(this) == DialogResult.OK)
                {
                    this.AddPaths(new string[] { dlg.SelectedPath });
                }
            }
        }

        private void BtnClear_Click(object sender, EventArgs e)
        {
            if (_sending)
            {
                SetStatus("正在传输，等结束再清空。");
                return;
            }
            _pending.Clear();
            _lstFiles.Items.Clear();
            _txtLog.Clear();
            _tipIndex = -1;
            _prg.Style = ProgressBarStyle.Continuous;
            _prg.Value = 0;
            _lblSpeed.Text = "";
            _speedSend.Reset();
            _speedT0Send = 0L;
            SetStatus("已清空。");
        }

        private void BtnSend_Click(object sender, EventArgs e)
        {
            if (_sending)
            {
                return;
            }
            if (_pending.Count == 0)
            {
                SetStatus("待传列表是空的：先拖文件进来，或用「添加文件」。");
                return;
            }

            string[] paths = _pending.ToArray();

            _sending = true;
            _btnSend.Enabled = false;
            _btnAddFile.Enabled = false;
            _btnAddFolder.Enabled = false;
            _btnClear.Enabled = false;

            _prg.Style = ProgressBarStyle.Continuous;
            _prg.Value = 0;
            _speedSend.Reset();
            _speedT0Send = 0L;
            _lblSpeed.Text = "";
            SetStatus(string.Format("正在发送 {0} 项到鸿蒙桌面…", paths.Length));
            AppendLog(string.Format("[UI] 开始发送 {0} 项 -> {1}", paths.Length, HdcCore.RemoteDesktop));

            Thread worker = new Thread(delegate() { this.SendWorker(paths); });
            worker.IsBackground = true;
            worker.Start();
        }

        // ================================================================ 发送页：传输线程

        private void SendWorker(string[] paths)
        {
            TransferResult result;
            try
            {
                result = HdcCore.Send(
                    paths,
                    HdcCore.RemoteDesktop,
                    new Action<long, long>(this.OnProgress),
                    new Action<string>(this.OnLog));
            }
            catch (TransferException ex)
            {
                result = new TransferResult();
                result.Success = false;
                result.Message = "发送失败：" + ex.Message;
                result.FileCount = 0;
            }
            catch (Exception ex)
            {
                result = new TransferResult();
                result.Success = false;
                result.Message = string.Format("发送失败（{0}）：{1}", ex.GetType().Name, ex.Message);
                result.FileCount = 0;
            }

            this.UiInvoke(delegate() { this.ApplyResult(result); });
        }

        // onProgress：可能在后台线程，也可能在 UI 线程，统一 marshal
        private void OnProgress(long done, long total)
        {
            string sp = this.TickSpeed(_speedSend, ref _speedT0Send, done);
            this.UiInvoke(delegate()
            {
                ApplyProgressTo(_prg, done, total);
                if (_lblSpeed != null && sp != null)
                {
                    _lblSpeed.Text = sp;
                }
            });
        }

        /// <summary>喂速率计并格式化成「12.3 MB/s」。返回 null 表示样本不足、暂不刷新。</summary>
        private string TickSpeed(SpeedMeter m, ref long t0, long done)
        {
            long now = Environment.TickCount;
            if (t0 == 0L)
            {
                t0 = now;
            }
            double bps = m.Feed(done, now - t0);
            if (bps <= 0.0)
            {
                return null;
            }
            return SpeedMeter.FormatBps(bps);
        }

        // onLog：同上
        private void OnLog(string line)
        {
            this.UiInvoke(delegate() { this.AppendLog(line); });
        }

        /// <summary>total > 0 → 百分比（done 超 total 钳到 100）；total == 0 → 不确定进度跑马灯。</summary>
        private static void ApplyProgressTo(FlatProgress prg, long done, long total)
        {
            if (prg == null)
            {
                return;
            }
            if (total > 0L)
            {
                if (prg.Style != ProgressBarStyle.Continuous)
                {
                    prg.Style = ProgressBarStyle.Continuous;
                }
                long pct = done * 100L / total;
                if (pct < 0L)
                {
                    pct = 0L;
                }
                if (pct > 100L)
                {
                    pct = 100L;
                }
                int v = (int)pct;
                if (v < prg.Minimum)
                {
                    v = prg.Minimum;
                }
                if (v > prg.Maximum)
                {
                    v = prg.Maximum;
                }
                prg.Value = v;
            }
            else
            {
                // total == 0：目录传输等不确定进度
                if (prg.Style != ProgressBarStyle.Marquee)
                {
                    prg.Style = ProgressBarStyle.Marquee;
                    prg.MarqueeAnimationSpeed = 30;
                }
            }
        }

        private void ApplyResult(TransferResult result)
        {
            _sending = false;
            _btnSend.Enabled = true;
            _btnAddFile.Enabled = true;
            _btnAddFolder.Enabled = true;
            _btnClear.Enabled = true;

            bool ok = result != null && result.Success;

            if (_prg.Style == ProgressBarStyle.Marquee)
            {
                _prg.MarqueeAnimationSpeed = 0;
                _prg.Style = ProgressBarStyle.Continuous;
            }
            _prg.Style = ProgressBarStyle.Continuous;
            _prg.Value = ok ? 100 : 0;

            string msg = (result == null) ? "发送结束，但没有拿到结果。" : result.Message;
            if (msg == null || msg.Length == 0)
            {
                msg = ok ? "发送完成。" : "发送失败。";
            }
            SetStatus(msg);
            _lblStatus.ForeColor = ok ? Color.FromArgb(0, 100, 0) : Color.Firebrick;
            if (ok && result != null && result.TotalBytes > 0L && result.ElapsedMs > 0L)
            {
                _lblSpeed.Text = HdcCore.FormatSpeed(result.TotalBytes, result.ElapsedMs) + " 平均";
            }

            if (result != null)
            {
                AppendLog(string.Format("[UI] {0}：{1}（文件 {2} 个，共 {3}，耗时 {4} ms）",
                    ok ? "成功" : "失败", msg, result.FileCount,
                    FormatSize(result.TotalBytes), result.ElapsedMs));
            }
            else
            {
                AppendLog("[UI] 结果为空。");
            }
        }

        // ================================================================ 接收页：刷新列表

        private void CmbRemoteDir_Changed(object sender, EventArgs e)
        {
            string dir = _cmbRemoteDir.SelectedItem as string;
            if (dir == null)
            {
                return;
            }
            SetRecvStatus(string.Format("已选择 {0}，点「刷新列表」拉取目录内容。", dir), false);
        }

        private void BtnRefresh_Click(object sender, EventArgs e)
        {
            if (_pulling)
            {
                SetRecvError("正在接收，等接收结束再刷新。");
                return;
            }
            if (_refreshing)
            {
                return;
            }
            string dir = CurrentRemoteDir();
            if (dir == null || dir.Length == 0)
            {
                SetRecvError("先在上面的下拉框里选一个远程目录。");
                return;
            }

            _refreshing = true;
            _btnRefresh.Enabled = false;
            _cmbRemoteDir.Enabled = false;
            SetRecvStatus(string.Format("正在读取 {0} …", dir), false);
            AppendRecvLog(string.Format("[UI] 开始列出 {0}", dir));

            Thread worker = new Thread(delegate() { this.RefreshWorker(dir); });
            worker.IsBackground = true;
            worker.Start();
        }

        private string CurrentRemoteDir()
        {
            string dir = _cmbRemoteDir.SelectedItem as string;
            if (dir == null || dir.Length == 0)
            {
                string text = _cmbRemoteDir.Text;
                if (text != null && text.Length > 0)
                {
                    dir = text;
                }
            }
            return dir;
        }

        private void RefreshWorker(string dir)
        {
            RemoteItem[] items = null;
            string err = null;
            try
            {
                items = HdcCore.ListRemote(dir);
            }
            catch (TransferException ex)
            {
                err = ex.Message;
            }
            catch (Exception ex)
            {
                err = string.Format("{0}：{1}", ex.GetType().Name, ex.Message);
            }

            this.UiInvoke(delegate() { this.ApplyRemoteList(items, err, dir); });
        }

        private void ApplyRemoteList(RemoteItem[] items, string err, string dir)
        {
            _refreshing = false;
            _btnRefresh.Enabled = true;
            _cmbRemoteDir.Enabled = true;

            if (err != null)
            {
                SetRecvError("刷新失败：" + err);
                AppendRecvLog("[UI] 刷新失败：" + err);
                return;
            }
            if (items == null || items.Length == 0)
            {
                _lvRemote.BeginUpdate();
                _lvRemote.Items.Clear();
                _lvRemote.EndUpdate();
                SetRecvStatus(string.Format("{0} 里没有可显示的内容。", dir), false);
                AppendRecvLog(string.Format("[UI] {0} 为空", dir));
                return;
            }

            _lvRemote.BeginUpdate();
            try
            {
                _lvRemote.Items.Clear();
                for (int i = 0; i < items.Length; i++)
                {
                    RemoteItem it = items[i];
                    if (it == null)
                    {
                        continue;
                    }
                    string name = it.Name;
                    if (name == null || name.Length == 0)
                    {
                        name = it.FullPath;
                    }
                    ListViewItem lvi = new ListViewItem(it.IsDir ? "文件夹" : "文件");
                    lvi.SubItems.Add(name == null ? string.Empty : name);
                    lvi.SubItems.Add(it.IsDir ? string.Empty : HdcCore.FormatBytes(it.Size));
                    lvi.Tag = it;                                  // Tag 存 RemoteItem
                    if (it.FullPath != null)
                    {
                        lvi.Name = it.FullPath;
                    }
                    _lvRemote.Items.Add(lvi);
                }
            }
            finally
            {
                _lvRemote.EndUpdate();
            }

            if (_sortCol >= 0 && _sortDir != SortNone)
            {
                this.ApplyRemoteSort();
            }

            SetRecvStatus(string.Format("已列出 {0} 项：{1}", _lvRemote.Items.Count, dir), true);
            AppendRecvLog(string.Format("[UI] 列出 {0} 共 {1} 项", dir, _lvRemote.Items.Count));
        }

        // ---------------------------------------------------------------- 接收页：排序

        private void LvRemote_ColumnClick(object sender, ColumnClickEventArgs e)
        {
            if (e.Column == _sortCol)
            {
                _sortDir = (_sortDir == SortAsc) ? SortDesc : SortAsc;
            }
            else
            {
                _sortCol = e.Column;
                _sortDir = SortAsc;
            }
            this.ApplyRemoteSort();
        }

        private void ApplyRemoteSort()
        {
            if (_sortCol < 0 || _sortDir == SortNone || _lvRemote.Items.Count < 2)
            {
                return;
            }
            List<ListViewItem> arr = new List<ListViewItem>();
            for (int i = 0; i < _lvRemote.Items.Count; i++)
            {
                arr.Add(_lvRemote.Items[i]);
            }
            arr.Sort(this.CompareRemoteItems);

            _lvRemote.BeginUpdate();
            try
            {
                _lvRemote.Items.Clear();
                for (int i = 0; i < arr.Count; i++)
                {
                    _lvRemote.Items.Add(arr[i]);
                }
            }
            finally
            {
                _lvRemote.EndUpdate();
            }
        }

        private int CompareRemoteItems(ListViewItem a, ListViewItem b)
        {
            RemoteItem ra = a.Tag as RemoteItem;
            RemoteItem rb = b.Tag as RemoteItem;
            int c;
            if (_sortCol == ColKind)
            {
                // 目录排前面
                string ka = (ra != null && ra.IsDir) ? "0" : "1";
                string kb = (rb != null && rb.IsDir) ? "0" : "1";
                c = string.Compare(ka, kb, StringComparison.Ordinal);
            }
            else if (_sortCol == ColSize)
            {
                long sa = (ra != null) ? ra.Size : 0L;
                long sb = (rb != null) ? rb.Size : 0L;
                c = sa.CompareTo(sb);
            }
            else
            {
                string na = (ra != null && ra.Name != null) ? ra.Name : string.Empty;
                string nb = (rb != null && rb.Name != null) ? rb.Name : string.Empty;
                c = string.Compare(na, nb, StringComparison.CurrentCultureIgnoreCase);
            }
            return (_sortDir == SortDesc) ? -c : c;
        }

        // ---------------------------------------------------------------- 接收页：目录下钻

        private void LvRemote_DoubleClick(object sender, EventArgs e)
        {
            if (_pulling || _refreshing)
            {
                return;
            }
            if (_lvRemote.SelectedItems.Count != 1)
            {
                return;
            }
            RemoteItem it = _lvRemote.SelectedItems[0].Tag as RemoteItem;
            if (it == null || !it.IsDir || it.FullPath == null)
            {
                return;
            }
            this.NavigateTo(it.FullPath);
        }

        private void NavigateTo(string dir)
        {
            int idx = -1;
            for (int i = 0; i < _cmbRemoteDir.Items.Count; i++)
            {
                string s = _cmbRemoteDir.Items[i] as string;
                if (s != null && string.Equals(s, dir, StringComparison.Ordinal))
                {
                    idx = i;
                    break;
                }
            }
            if (idx < 0)
            {
                // 下钻进来的子目录临时插到列表最前，方便返回时再选根目录
                _cmbRemoteDir.Items.Insert(0, dir);
                idx = 0;
            }
            _cmbRemoteDir.SelectedIndex = idx;
            this.BtnRefresh_Click(null, EventArgs.Empty);
        }

        // ---------------------------------------------------------------- 接收页：接收

        private void BtnPull_Click(object sender, EventArgs e)
        {
            if (_refreshing || _pulling)
            {
                return;
            }
            List<string> sel = new List<string>();
            for (int i = 0; i < _lvRemote.SelectedItems.Count; i++)
            {
                RemoteItem it = _lvRemote.SelectedItems[i].Tag as RemoteItem;
                if (it != null && it.FullPath != null && it.FullPath.Length > 0)
                {
                    sel.Add(it.FullPath);
                }
            }
            if (sel.Count == 0)
            {
                SetRecvError("先在上面选中要接收的文件或文件夹。");
                return;
            }
            this.StartPull(sel.ToArray());
        }

        private void BtnPullAll_Click(object sender, EventArgs e)
        {
            if (_refreshing || _pulling)
            {
                return;
            }
            if (_lvRemote.Items.Count == 0)
            {
                SetRecvError("列表是空的：先点「刷新列表」。");
                return;
            }
            List<string> all = new List<string>();
            for (int i = 0; i < _lvRemote.Items.Count; i++)
            {
                RemoteItem it = _lvRemote.Items[i].Tag as RemoteItem;
                if (it != null && it.FullPath != null && it.FullPath.Length > 0)
                {
                    all.Add(it.FullPath);
                }
            }
            if (all.Count == 0)
            {
                SetRecvError("列表里没有可接收的项。");
                return;
            }
            this.StartPull(all.ToArray());
        }

        private void StartPull(string[] remotePaths)
        {
            _pulling = true;
            _pullCount = remotePaths.Length;

            _btnPull.Enabled = false;
            _btnPullAll.Enabled = false;
            _btnRefresh.Enabled = false;
            _cmbRemoteDir.Enabled = false;
            _lvRemote.Enabled = false;

            _prgRecv.Style = ProgressBarStyle.Continuous;
            _prgRecv.Value = 0;
            _speedRecv.Reset();
            _speedT0Recv = 0L;
            _lblSpeedRecv.Text = "";
            SetRecvStatus(string.Format("正在接收 {0} 项到 {1} …", _pullCount, _localDir), false);
            AppendRecvLog(string.Format("[UI] 开始接收 {0} 项 -> {1}", _pullCount, _localDir));

            Thread worker = new Thread(delegate() { this.PullWorker(remotePaths); });
            worker.IsBackground = true;
            worker.Start();
        }

        private void PullWorker(string[] remotePaths)
        {
            TransferResult result;
            try
            {
                result = HdcCore.Pull(
                    remotePaths,
                    _localDir,
                    new Action<long, long>(this.OnRecvProgress),
                    new Action<string>(this.OnRecvLog));
            }
            catch (TransferException ex)
            {
                result = new TransferResult();
                result.Success = false;
                result.Message = "接收失败：" + ex.Message;
                result.FileCount = 0;
            }
            catch (Exception ex)
            {
                result = new TransferResult();
                result.Success = false;
                result.Message = string.Format("接收失败（{0}）：{1}", ex.GetType().Name, ex.Message);
                result.FileCount = 0;
            }

            this.UiInvoke(delegate() { this.ApplyPullResult(result); });
        }

        private void OnRecvProgress(long done, long total)
        {
            string sp = this.TickSpeed(_speedRecv, ref _speedT0Recv, done);
            this.UiInvoke(delegate()
            {
                ApplyProgressTo(_prgRecv, done, total);
                if (_lblSpeedRecv != null && sp != null)
                {
                    _lblSpeedRecv.Text = sp;
                }
            });
        }

        private void OnRecvLog(string line)
        {
            this.UiInvoke(delegate() { this.AppendRecvLog(line); });
        }

        private void ApplyPullResult(TransferResult result)
        {
            _pulling = false;
            _btnPull.Enabled = true;
            _btnPullAll.Enabled = true;
            _btnRefresh.Enabled = true;
            _cmbRemoteDir.Enabled = true;
            _lvRemote.Enabled = true;

            bool ok = result != null && result.Success;

            if (_prgRecv.Style == ProgressBarStyle.Marquee)
            {
                _prgRecv.MarqueeAnimationSpeed = 0;
                _prgRecv.Style = ProgressBarStyle.Continuous;
            }
            _prgRecv.Style = ProgressBarStyle.Continuous;
            _prgRecv.Value = ok ? 100 : 0;

            string msg = (result == null) ? "接收结束，但没有拿到结果。" : result.Message;
            if (msg == null || msg.Length == 0)
            {
                msg = ok ? "接收完成。" : "接收失败。";
            }
            if (ok)
            {
                SetRecvStatus(msg, true);
                if (result != null && result.TotalBytes > 0L && result.ElapsedMs > 0L)
                {
                    _lblSpeedRecv.Text = HdcCore.FormatSpeed(result.TotalBytes, result.ElapsedMs) + " 平均";
                }
            }
            else
            {
                SetRecvError(msg);
            }

            if (result != null)
            {
                AppendRecvLog(string.Format("[UI] {0}：{1}（文件 {2} 个，共 {3}，耗时 {4} ms）",
                    ok ? "成功" : "失败", msg, result.FileCount,
                    HdcCore.FormatBytes(result.TotalBytes), result.ElapsedMs));
            }
            else
            {
                AppendRecvLog("[UI] 结果为空。");
            }
        }

        // ---------------------------------------------------------------- 接收页：打开本地文件夹

        private void BtnOpenLocal_Click(object sender, EventArgs e)
        {
            try
            {
                if (!Directory.Exists(_localDir))
                {
                    Directory.CreateDirectory(_localDir);
                }
                Process.Start("explorer.exe", "\"" + _localDir + "\"");
                SetRecvStatus(string.Format("已在资源管理器打开 {0}", _localDir), true);
            }
            catch (Exception ex)
            {
                SetRecvError(string.Format("打开本地文件夹失败（{0}）：{1}", ex.GetType().Name, ex.Message));
            }
        }
    }
}
