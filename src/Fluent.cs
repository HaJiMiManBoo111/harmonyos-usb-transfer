using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace Lingxi
{
    /// <summary>
    /// Fluent 2 风格设计令牌 + 自绘控件。
    /// 配色取向：中性灰底 + 纯白内容面 + 单一强调蓝，饱和度低，长时间看不疲劳；
    /// 语义色（成功/危险）只用在状态点上，做到「重点一眼看到」。
    /// </summary>
    internal static class Fluent
    {
        // ---- 面 ----
        public static readonly Color WindowBg = Color.FromArgb(0xF3, 0xF3, 0xF3);
        public static readonly Color CardBg = Color.FromArgb(0xFF, 0xFF, 0xFF);
        public static readonly Color SubtleBg = Color.FromArgb(0xFA, 0xFA, 0xFA);
        public static readonly Color LogBg = Color.FromArgb(0xFB, 0xFB, 0xFB);

        // ---- 线 ----
        public static readonly Color Border = Color.FromArgb(0xE0, 0xE0, 0xE0);
        public static readonly Color BorderStrong = Color.FromArgb(0xC8, 0xC8, 0xC8);

        // ---- 字 ----
        public static readonly Color TextPrimary = Color.FromArgb(0x1F, 0x1F, 0x1F);
        public static readonly Color TextSecondary = Color.FromArgb(0x5C, 0x5C, 0x5C);
        public static readonly Color TextTertiary = Color.FromArgb(0x8A, 0x8A, 0x8A);

        // ---- 强调 ----
        public static readonly Color Accent = Color.FromArgb(0x0F, 0x6C, 0xBD);
        public static readonly Color AccentHover = Color.FromArgb(0x11, 0x5E, 0xA3);
        public static readonly Color AccentDown = Color.FromArgb(0x0C, 0x3B, 0x5E);
        public static readonly Color AccentSoft = Color.FromArgb(0xE5, 0xF0, 0xFA);

        // ---- 语义 ----
        public static readonly Color Success = Color.FromArgb(0x0F, 0x7B, 0x0F);
        public static readonly Color SuccessSoft = Color.FromArgb(0xE7, 0xF5, 0xE7);
        public static readonly Color Danger = Color.FromArgb(0xC4, 0x2B, 0x1C);
        public static readonly Color DangerSoft = Color.FromArgb(0xFC, 0xE8, 0xE6);
        public static readonly Color Warn = Color.FromArgb(0x9D, 0x5D, 0x00);
        public static readonly Color WarnSoft = Color.FromArgb(0xFB, 0xF0, 0xE0);

        // ---- DPI 缩放 ----
        // 全程序所有像素常量都以 96 DPI（100% 缩放）为基准书写，
        // 实际落屏时统一乘 Scale。这样 150% 缩放下字体（pt，天然随 DPI）与
        // 控件尺寸（px，需手动缩放）保持一致比例，不会出现字大框小。
        public static float Scale = 1f;

        /// <summary>按当前 DPI 缩放像素值（四舍五入）。</summary>
        public static int S(float px)
        {
            int v = (int)System.Math.Round(px * Scale);
            return v < 1 ? 1 : v;
        }

        /// <summary>按当前 DPI 缩放像素值（保留小数，用于画笔宽度）。</summary>
        public static float SF(float px)
        {
            return px * Scale;
        }

        /// <summary>扫描系统 DPI 并设定缩放比例。必须在创建任何控件之前调用。</summary>
        public static void InitDpi()
        {
            float dpi = 96f;
            try
            {
                using (Graphics g = Graphics.FromHwnd(IntPtr.Zero))
                {
                    dpi = g.DpiX;
                }
            }
            catch (Exception)
            {
                dpi = 96f;
            }
            if (dpi < 96f)
            {
                dpi = 96f;
            }
            Scale = dpi / 96f;
        }

        // ---- 其它 ----
        public static readonly Color Track = Color.FromArgb(0xE6, 0xE6, 0xE6);
        public static readonly Color SelectionBg = Color.FromArgb(0xE5, 0xF0, 0xFA);

        private static readonly Font _body = new Font("Microsoft YaHei UI", 9F, FontStyle.Regular);
        private static readonly Font _bodyStrong = new Font("Microsoft YaHei UI", 9F, FontStyle.Bold);
        private static readonly Font _title = new Font("Microsoft YaHei UI", 12F, FontStyle.Bold);
        private static readonly Font _section = new Font("Microsoft YaHei UI", 9F, FontStyle.Bold);
        private static readonly Font _hint = new Font("Microsoft YaHei UI", 12.5F, FontStyle.Regular);
        private static readonly Font _hintSub = new Font("Microsoft YaHei UI", 8.5F, FontStyle.Regular);
        private static readonly Font _mono = new Font("Consolas", 8.5F, FontStyle.Regular);

        /// <summary>正文字体（9pt）。</summary>
        public static Font Body { get { return _body; } }
        /// <summary>正文加粗（9pt）。</summary>
        public static Font BodyStrong { get { return _bodyStrong; } }
        /// <summary>标题（12pt 粗）。</summary>
        public static Font Title { get { return _title; } }
        /// <summary>小节标题（9pt 粗）。</summary>
        public static Font Section { get { return _section; } }
        /// <summary>拖拽区主提示（12.5pt）。</summary>
        public static Font Hint { get { return _hint; } }
        /// <summary>拖拽区副提示（8.5pt）。</summary>
        public static Font HintSub { get { return _hintSub; } }
        /// <summary>日志等宽（Consolas 8.5pt）。</summary>
        public static Font Mono { get { return _mono; } }

        /// <summary>圆角矩形路径。半径自动收敛到短边一半，避免小控件画歪。</summary>
        public static GraphicsPath RoundRect(Rectangle r, int radius)
        {
            GraphicsPath path = new GraphicsPath();
            if (r.Width < 2 || r.Height < 2)
            {
                path.AddRectangle(r);
                return path;
            }
            int d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
            if (d < 2)
            {
                path.AddRectangle(r);
                return path;
            }
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    // ================================================================== 按钮

    internal enum FluentButtonKind
    {
        /// <summary>强调色实心，一屏只放一个。</summary>
        Primary,
        /// <summary>白底描边，次要操作。</summary>
        Secondary,
        /// <summary>无底无边，最弱的第三级操作。</summary>
        Subtle
    }

    /// <summary>
    /// 扁平圆角按钮。悬停/按下用底色深浅表达，不画焦点框，不用系统灰渐变。
    /// </summary>
    internal class FluentButton : Button
    {
        private FluentButtonKind _kind = FluentButtonKind.Secondary;
        private bool _hover;
        private bool _down;

        public FluentButton()
        {
            this.SetStyle(ControlStyles.AllPaintingInWmPaint
                | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer
                | ControlStyles.ResizeRedraw, true);
            this.FlatStyle = FlatStyle.Flat;
            this.FlatAppearance.BorderSize = 0;
            this.UseVisualStyleBackColor = false;
            this.Font = Fluent.Body;
            this.Cursor = Cursors.Hand;
            this.Height = Fluent.S(30);
            this.BackColor = Fluent.WindowBg;
        }

        public FluentButtonKind Kind
        {
            get { return _kind; }
            set { _kind = value; this.Invalidate(); }
        }

        protected override void OnMouseEnter(EventArgs e)
        {
            _hover = true;
            this.Invalidate();
            base.OnMouseEnter(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hover = false;
            _down = false;
            this.Invalidate();
            base.OnMouseLeave(e);
        }

        protected override void OnMouseDown(MouseEventArgs mevent)
        {
            if (mevent.Button == MouseButtons.Left)
            {
                _down = true;
                this.Invalidate();
            }
            base.OnMouseDown(mevent);
        }

        protected override void OnMouseUp(MouseEventArgs mevent)
        {
            _down = false;
            this.Invalidate();
            base.OnMouseUp(mevent);
        }

        protected override void OnEnabledChanged(EventArgs e)
        {
            this.Invalidate();
            base.OnEnabledChanged(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Color parentBg = (this.Parent == null) ? Fluent.WindowBg : this.Parent.BackColor;
            using (SolidBrush bg = new SolidBrush(parentBg))
            {
                g.FillRectangle(bg, this.ClientRectangle);
            }

            Rectangle r = new Rectangle(0, 0, this.Width - 1, this.Height - 1);
            Color fill;
            Color border;
            Color fg;

            if (!this.Enabled)
            {
                fill = Fluent.SubtleBg;
                border = Fluent.Border;
                fg = Fluent.TextTertiary;
            }
            else if (_kind == FluentButtonKind.Primary)
            {
                fill = _down ? Fluent.AccentDown : (_hover ? Fluent.AccentHover : Fluent.Accent);
                border = fill;
                fg = Color.White;
            }
            else if (_kind == FluentButtonKind.Subtle)
            {
                fill = (_hover || _down) ? Fluent.AccentSoft : Color.Empty;
                border = Color.Empty;
                fg = (_hover || _down) ? Fluent.Accent : Fluent.TextSecondary;
            }
            else
            {
                fill = _down ? Fluent.AccentSoft : (_hover ? Fluent.SubtleBg : Fluent.CardBg);
                border = _hover ? Fluent.BorderStrong : Fluent.Border;
                fg = Fluent.TextPrimary;
            }

            using (GraphicsPath path = Fluent.RoundRect(r, Fluent.S(4)))
            {
                if (fill != Color.Empty)
                {
                    using (SolidBrush b = new SolidBrush(fill))
                    {
                        g.FillPath(b, path);
                    }
                }
                if (border != Color.Empty)
                {
                    using (Pen p = new Pen(border, 1F))
                    {
                        g.DrawPath(p, path);
                    }
                }
            }

            int inset = Fluent.S(2);
            Rectangle textRect = new Rectangle(inset, 0, this.Width - inset * 2, this.Height);
            TextRenderer.DrawText(g, this.Text, this.Font, textRect, fg,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter
                | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
        }
    }

    // ================================================================== 进度条

    /// <summary>
    /// 细条圆角进度。保留 ProgressBar 的 Style / Value / MarqueeAnimationSpeed 语义，
    /// 好让调用方（ApplyProgressTo）不用改判定逻辑。
    /// </summary>
    internal class FlatProgress : Control
    {
        private int _min;
        private int _max = 100;
        private int _value;
        private ProgressBarStyle _style = ProgressBarStyle.Continuous;
        private int _marqueeSpeed;
        private int _pos;
        private readonly Timer _timer;

        public FlatProgress()
        {
            this.SetStyle(ControlStyles.AllPaintingInWmPaint
                | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer
                | ControlStyles.ResizeRedraw, true);
            this.Height = Fluent.S(8);
            this.BackColor = Fluent.WindowBg;
            _timer = new Timer();
            _timer.Interval = 33;
            _timer.Tick += new EventHandler(this.OnTick);
        }

        public int Minimum
        {
            get { return _min; }
            set { _min = value; this.Invalidate(); }
        }

        public int Maximum
        {
            get { return _max; }
            set { _max = value; this.Invalidate(); }
        }

        public int Value
        {
            get { return _value; }
            set
            {
                _value = value;
                this.Invalidate();
            }
        }

        public ProgressBarStyle Style
        {
            get { return _style; }
            set
            {
                if (_style == value)
                {
                    return;
                }
                _style = value;
                this.SyncTimer();
                this.Invalidate();
            }
        }

        public int MarqueeAnimationSpeed
        {
            get { return _marqueeSpeed; }
            set
            {
                _marqueeSpeed = value;
                this.SyncTimer();
                this.Invalidate();
            }
        }

        private void SyncTimer()
        {
            bool run = (_style == ProgressBarStyle.Marquee) && (_marqueeSpeed > 0);
            if (run && !_timer.Enabled)
            {
                _timer.Start();
            }
            else if (!run && _timer.Enabled)
            {
                _timer.Stop();
            }
        }

        private void OnTick(object sender, EventArgs e)
        {
            _pos += 6;
            int span = Math.Max(40, this.Width / 3);
            if (_pos > this.Width + span)
            {
                _pos = -span;
            }
            this.Invalidate();
        }

        /// <summary>数值转百分比（钳制到 [0,100]）。</summary>
        private int Percent()
        {
            int range = _max - _min;
            if (range <= 0)
            {
                return 0;
            }
            int pct = (_value - _min) * 100 / range;
            if (pct < 0)
            {
                pct = 0;
            }
            if (pct > 100)
            {
                pct = 100;
            }
            return pct;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Color parentBg = (this.Parent == null) ? Fluent.WindowBg : this.Parent.BackColor;
            using (SolidBrush bg = new SolidBrush(parentBg))
            {
                g.FillRectangle(bg, this.ClientRectangle);
            }

            int h = this.Height;
            Rectangle track = new Rectangle(0, 0, this.Width - 1, h - 1);
            using (GraphicsPath path = Fluent.RoundRect(track, h / 2))
            {
                using (SolidBrush b = new SolidBrush(Fluent.Track))
                {
                    g.FillPath(b, path);
                }

                if (_style == ProgressBarStyle.Marquee)
                {
                    int span = Math.Max(40, this.Width / 3);
                    int x = _pos;
                    int w = Math.Min(span, this.Width);
                    if (x + w > this.Width)
                    {
                        w = this.Width - x;
                    }
                    if (x < 0)
                    {
                        w += x;
                        x = 0;
                    }
                    if (w > 2)
                    {
                        Rectangle mr = new Rectangle(x, 0, w, h - 1);
                        using (GraphicsPath mp = Fluent.RoundRect(mr, h / 2))
                        using (SolidBrush mb = new SolidBrush(Fluent.Accent))
                        {
                            g.FillPath(mb, mp);
                        }
                    }
                }
                else
                {
                    int pct = this.Percent();
                    if (pct > 0)
                    {
                        int fw = this.Width * pct / 100;
                        if (fw < h)
                        {
                            fw = Math.Min(this.Width, h);
                        }
                        Rectangle fr = new Rectangle(0, 0, fw, h - 1);
                        using (GraphicsPath fp = Fluent.RoundRect(fr, h / 2))
                        {
                            Color c1 = Fluent.Accent;
                            Color c2 = Color.FromArgb(0x2B, 0x88, 0xD8);
                            using (LinearGradientBrush lb = new LinearGradientBrush(
                                new Rectangle(0, 0, Math.Max(fw, 2), Math.Max(h, 2)), c1, c2, LinearGradientMode.Horizontal))
                            {
                                g.FillPath(lb, fp);
                            }
                        }
                    }
                }
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && _timer != null)
            {
                _timer.Stop();
                _timer.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    // ================================================================== 卡片

    /// <summary>白底圆角描边容器，用来把功能块分组，视觉上「一眼分得清」。</summary>
    internal class CardPanel : Panel
    {
        private string _caption;

        public CardPanel()
        {
            this.SetStyle(ControlStyles.AllPaintingInWmPaint
                | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer
                | ControlStyles.ResizeRedraw, true);
            this.BackColor = Fluent.CardBg;
        }

        /// <summary>左上角小标题，空则不画。</summary>
        public string Caption
        {
            get { return _caption; }
            set { _caption = value; this.Invalidate(); }
        }

        /// <summary>标题占用的顶部高度（0 表示无标题）。</summary>
        public int CaptionHeight
        {
            get { return string.IsNullOrEmpty(_caption) ? 0 : Fluent.S(22); }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Color parentBg = (this.Parent == null) ? Fluent.WindowBg : this.Parent.BackColor;
            using (SolidBrush bg = new SolidBrush(parentBg))
            {
                g.FillRectangle(bg, this.ClientRectangle);
            }

            Rectangle r = new Rectangle(0, 0, this.Width - 1, this.Height - 1);
            using (GraphicsPath path = Fluent.RoundRect(r, Fluent.S(6)))
            {
                using (SolidBrush b = new SolidBrush(Fluent.CardBg))
                {
                    g.FillPath(b, path);
                }
                using (Pen p = new Pen(Fluent.Border, 1F))
                {
                    g.DrawPath(p, path);
                }
            }

            if (!string.IsNullOrEmpty(_caption))
            {
                Rectangle tr = new Rectangle(Fluent.S(12), Fluent.S(5), this.Width - Fluent.S(24), Fluent.S(18));
                TextRenderer.DrawText(g, _caption, Fluent.Section, tr, Fluent.TextSecondary,
                    TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);
            }
        }
    }

    // ================================================================== 状态胶囊

    /// <summary>
    /// 设备状态胶囊：左侧语义色圆点 + 文字。
    /// 这是窗口里唯一用高饱和色的地方，保证「重点一眼看到」。
    /// </summary>
    internal class StatusPill : Control
    {
        private int _state;   // 0=进行中 1=正常 2=异常
        private string _text = "";

        public StatusPill()
        {
            this.SetStyle(ControlStyles.AllPaintingInWmPaint
                | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer
                | ControlStyles.ResizeRedraw, true);
            this.BackColor = Fluent.CardBg;
            this.Font = Fluent.Body;
            this.Height = Fluent.S(28);
        }

        public bool Ok
        {
            get { return _state == 1; }
        }

        /// <summary>更新状态与文案（true=正常，false=异常）。</summary>
        public void SetState(bool ok, string text)
        {
            _state = ok ? 1 : 2;
            _text = (text == null) ? string.Empty : text;
            this.Invalidate();
        }

        /// <summary>进行中：既不是成功也不是失败，用于「正在检测设备…」这类过渡态。</summary>
        public void SetPending(string text)
        {
            _state = 0;
            _text = (text == null) ? string.Empty : text;
            this.Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Color parentBg = (this.Parent == null) ? Fluent.WindowBg : this.Parent.BackColor;
            using (SolidBrush bg = new SolidBrush(parentBg))
            {
                g.FillRectangle(bg, this.ClientRectangle);
            }

            Color dot = Fluent.Accent;
            Color fill = Fluent.AccentSoft;
            if (_state == 1) { dot = Fluent.Success; fill = Fluent.SuccessSoft; }
            else if (_state == 2) { dot = Fluent.Danger; fill = Fluent.DangerSoft; }

            Rectangle r = new Rectangle(0, 0, this.Width - 1, this.Height - 1);
            using (GraphicsPath path = Fluent.RoundRect(r, this.Height / 2))
            {
                using (SolidBrush b = new SolidBrush(fill))
                {
                    g.FillPath(b, path);
                }
                using (Pen p = new Pen(dot, 1F))
                {
                    g.DrawPath(p, path);
                }
            }

            int cy = this.Height / 2;
            int dotD = Fluent.S(8);
            int dotX = Fluent.S(11);
            using (SolidBrush b = new SolidBrush(dot))
            {
                g.FillEllipse(b, dotX, cy - dotD / 2, dotD, dotD);
            }
            int textX = dotX + dotD + Fluent.S(6);
            Rectangle tr = new Rectangle(textX, 0, this.Width - textX - Fluent.S(8), this.Height);
            TextRenderer.DrawText(g, _text, this.Font, tr, dot,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter
                | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
        }
    }

    // ================================================================== 标签页

    /// <summary>
    /// 自绘标签条：选中项强调色文字 + 底部强调线，未选中为次级灰。
    /// 只接管标签头，页面区仍由 TabControl 自己管，避免整控件重绘带来的布局风险。
    /// </summary>
    internal class FluentTabs : TabControl
    {
        /// <summary>标签条总高（ItemSize 高 + Padding 上下 + 边框），随 DPI 缩放。</summary>
        public static int StripHeightPx
        {
            get { return Fluent.S(34) + Fluent.S(4) * 2 + Fluent.S(4); }
        }

        private readonly Font _sel;
        private readonly Font _norm;

        public FluentTabs()
        {
            this.SetStyle(ControlStyles.OptimizedDoubleBuffer, true);
            this.Font = Fluent.Body;
            this.BackColor = Fluent.WindowBg;
            this.DrawMode = TabDrawMode.OwnerDrawFixed;
            this.SizeMode = TabSizeMode.Fixed;
            this.ItemSize = new Size(Fluent.S(120), Fluent.S(34));
            this.Padding = new Point(Fluent.S(14), Fluent.S(4));
            _sel = new Font("Microsoft YaHei UI", 9.5F, FontStyle.Bold);
            _norm = new Font("Microsoft YaHei UI", 9.5F, FontStyle.Regular);
        }

        protected override void OnDrawItem(DrawItemEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            Rectangle r;
            try
            {
                r = this.GetTabRect(e.Index);
            }
            catch (Exception)
            {
                return;
            }
            if (r.Width < 4 || r.Height < 4)
            {
                return;
            }

            bool selected = (e.Index == this.SelectedIndex);
            using (SolidBrush bg = new SolidBrush(Fluent.WindowBg))
            {
                g.FillRectangle(bg, r);
            }

            if (selected)
            {
                Rectangle pill = new Rectangle(r.X + Fluent.S(6), r.Y + Fluent.S(4),
                    r.Width - Fluent.S(12), r.Height - Fluent.S(8));
                using (GraphicsPath path = Fluent.RoundRect(pill, Fluent.S(5)))
                using (SolidBrush b = new SolidBrush(Fluent.CardBg))
                {
                    g.FillPath(b, path);
                }
            }

            string label = String.Empty;
            try
            {
                label = this.TabPages[e.Index].Text;
            }
            catch (Exception)
            {
                label = String.Empty;
            }

            Color fg = selected ? Fluent.Accent : Fluent.TextSecondary;
            Font f = selected ? _sel : _norm;
            TextRenderer.DrawText(g, label, f, r, fg,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding);

            if (selected)
            {
                using (Pen p = new Pen(Fluent.Accent, Fluent.SF(2F)))
                {
                    p.StartCap = LineCap.Round;
                    p.EndCap = LineCap.Round;
                    int ux = Fluent.S(22);
                    int uy = r.Bottom - Fluent.S(5);
                    g.DrawLine(p, r.X + ux, uy, r.Right - ux, uy);
                }
            }
        }
    }

    // ================================================================== 可拖动分隔条

    /// <summary>
    /// 列表 / 日志之间的分隔条：按住上下拖动改变两块的高度比例。
    /// 只负责交互与外观，比例由宿主（MainForm）计算。
    /// </summary>
    internal class GripBar : Control
    {
        private bool _hot;
        private bool _down;

        public event EventHandler Moved;

        public GripBar()
        {
            this.SetStyle(ControlStyles.AllPaintingInWmPaint
                | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer
                | ControlStyles.ResizeRedraw, true);
            this.BackColor = Fluent.WindowBg;
            this.Cursor = Cursors.SizeNS;
            this.Height = Fluent.S(12);
        }

        public bool Dragging
        {
            get { return _down; }
        }

        protected override void OnMouseEnter(EventArgs e)
        {
            _hot = true;
            this.Invalidate();
            base.OnMouseEnter(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hot = false;
            if (!_down)
            {
                this.Invalidate();
            }
            base.OnMouseLeave(e);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Left)
            {
                _down = true;
                this.Capture = true;
                this.Invalidate();
            }
            base.OnMouseDown(e);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            if (_down && this.Moved != null)
            {
                this.Moved(this, EventArgs.Empty);
            }
            base.OnMouseMove(e);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            if (_down && e.Button == MouseButtons.Left)
            {
                _down = false;
                this.Capture = false;
                this.Invalidate();
            }
            base.OnMouseUp(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Color parentBg = (this.Parent == null) ? Fluent.WindowBg : this.Parent.BackColor;
            using (SolidBrush bg = new SolidBrush(parentBg))
            {
                g.FillRectangle(bg, this.ClientRectangle);
            }

            // 中间一根短横杠：平时浅灰，悬停/拖动时变成强调蓝
            Color bar = (_down || _hot) ? Fluent.Accent : Fluent.BorderStrong;
            int w = Math.Min(Fluent.S(44), this.Width - Fluent.S(8));
            int h = Fluent.S(4);
            int x = (this.Width - w) / 2;
            int y = (this.Height - h) / 2;
            using (GraphicsPath path = Fluent.RoundRect(new Rectangle(x, y, w, h), h / 2))
            using (SolidBrush b = new SolidBrush(bar))
            {
                g.FillPath(b, path);
            }
        }
    }
}
