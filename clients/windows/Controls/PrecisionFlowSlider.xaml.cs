using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Effects;
using System.Windows.Threading;

namespace XScope.Controls;

/// <summary>
/// 4-step precision slider with a seamless looping 流光 fill and a large drag hit area.
/// </summary>
public partial class PrecisionFlowSlider : UserControl
{
    public static readonly DependencyProperty ValueProperty =
        DependencyProperty.Register(
            nameof(Value),
            typeof(int),
            typeof(PrecisionFlowSlider),
            new FrameworkPropertyMetadata(
                1,
                FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
                OnValueChanged));

    public static readonly DependencyProperty FlowActiveProperty =
        DependencyProperty.Register(
            nameof(FlowActive),
            typeof(bool),
            typeof(PrecisionFlowSlider),
            new PropertyMetadata(false, OnFlowActiveChanged));

    private const int Steps = 3; // values 0..3
    private const double RailInset = 14;
    private const double FlowBandWidth = 140;
    private const double FlowPixelsPerSecond = 120;

    private bool _dragging;
    private double _visualT;
    private bool _flowRunning;
    private double _flowOffset;
    private DispatcherTimer? _flowTimer;
    private int _fadeEpoch; // invalidates in-flight fade Completed handlers

    public PrecisionFlowSlider()
    {
        InitializeComponent();
        Loaded += (_, _) =>
        {
            SizeChanged += (_, _) => LayoutChrome(animateThumb: false);
            _visualT = Value / (double)Steps;
            LayoutChrome(animateThumb: false);
            SyncFlow(FlowActive);
        };
        Unloaded += (_, _) => StopFlow(immediate: true);

        HitStrip.PreviewMouseLeftButtonDown += OnPointerDown;
        HitStrip.PreviewMouseMove += OnPointerMove;
        HitStrip.PreviewMouseLeftButtonUp += OnPointerUp;
        HitStrip.LostMouseCapture += (_, _) =>
        {
            if (_dragging)
            {
                EndDrag();
            }
        };
        PreviewKeyDown += OnKeyDown;
    }

    public int Value
    {
        get => (int)GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public bool FlowActive
    {
        get => (bool)GetValue(FlowActiveProperty);
        set => SetValue(FlowActiveProperty, value);
    }

    public event RoutedPropertyChangedEventHandler<int>? ValueChanged;

    /// <summary>Refresh rail / accent paints for light↔dark theme switches.</summary>
    public void ApplyThemeColors(Color accent, Color railIdle, bool dark)
    {
        Rail.Background = new SolidColorBrush(railIdle);
        FillBase.Background = new SolidColorBrush(accent);
        ThumbDot.Fill = new SolidColorBrush(accent);
        ThumbDot.Stroke = new SolidColorBrush(dark
            ? Color.FromRgb(0x16, 0x18, 0x1C)
            : Colors.White);
        ThumbHalo.Fill = new SolidColorBrush(Color.FromArgb(0x55, accent.R, accent.G, accent.B));
        if (ThumbShadow is not null)
        {
            ThumbShadow.Color = dark ? Colors.Black : Color.FromRgb(0x20, 0x21, 0x24);
        }

        RailGlow.Background = new SolidColorBrush(Color.FromArgb(0x40, accent.R, accent.G, accent.B));
        if (RailGlow.Effect is DropShadowEffect glowFx)
        {
            glowFx.Color = accent;
        }
    }

    private static void OnValueChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not PrecisionFlowSlider slider)
        {
            return;
        }

        var next = Math.Clamp((int)e.NewValue, 0, Steps);
        if (next != (int)e.NewValue)
        {
            slider.Value = next;
            return;
        }

        if (!slider._dragging)
        {
            slider._visualT = next / (double)Steps;
            slider.LayoutChrome(animateThumb: true);
        }

        slider.ValueChanged?.Invoke(
            slider,
            new RoutedPropertyChangedEventArgs<int>((int)e.OldValue, next));
    }

    private static void OnFlowActiveChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is PrecisionFlowSlider slider)
        {
            slider.SyncFlow(e.NewValue is true);
        }
    }

    private void OnPointerDown(object sender, MouseButtonEventArgs e)
    {
        _dragging = true;
        HitStrip.CaptureMouse();
        Focus();
        ApplyPointer(e.GetPosition(HitStrip), commitStep: true);
        e.Handled = true;
    }

    private void OnPointerMove(object sender, MouseEventArgs e)
    {
        if (!_dragging || e.LeftButton != MouseButtonState.Pressed)
        {
            return;
        }

        ApplyPointer(e.GetPosition(HitStrip), commitStep: true);
        e.Handled = true;
    }

    private void OnPointerUp(object sender, MouseButtonEventArgs e)
    {
        if (!_dragging)
        {
            return;
        }

        ApplyPointer(e.GetPosition(HitStrip), commitStep: true);
        EndDrag();
        e.Handled = true;
    }

    private void EndDrag()
    {
        _dragging = false;
        if (HitStrip.IsMouseCaptured)
        {
            HitStrip.ReleaseMouseCapture();
        }

        _visualT = Value / (double)Steps;
        LayoutChrome(animateThumb: true);
    }

    private void OnKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key is Key.Left or Key.Down)
        {
            Value = Math.Clamp(Value - 1, 0, Steps);
            e.Handled = true;
        }
        else if (e.Key is Key.Right or Key.Up)
        {
            Value = Math.Clamp(Value + 1, 0, Steps);
            e.Handled = true;
        }
        else if (e.Key is Key.Home)
        {
            Value = 0;
            e.Handled = true;
        }
        else if (e.Key is Key.End)
        {
            Value = Steps;
            e.Handled = true;
        }
    }

    private void ApplyPointer(Point p, bool commitStep)
    {
        var rail = Math.Max(1, HitStrip.ActualWidth - RailInset * 2);
        var x = Math.Clamp(p.X - RailInset, 0, rail);
        _visualT = x / rail;

        if (commitStep)
        {
            var stepped = (int)Math.Round(_visualT * Steps);
            Value = Math.Clamp(stepped, 0, Steps);
        }

        LayoutChrome(animateThumb: false);
    }

    private void LayoutChrome(bool animateThumb)
    {
        if (!IsLoaded || HitStrip.ActualWidth < 8)
        {
            return;
        }

        var rail = Math.Max(1, HitStrip.ActualWidth - RailInset * 2);
        var t = _dragging ? _visualT : Value / (double)Steps;
        t = Math.Clamp(t, 0, 1);
        var fillW = rail * t;

        FillHost.Width = Math.Max(fillW, t > 0.001 ? 10 : 0);

        var thumbX = fillW - (Thumb.Width / 2);
        thumbX = Math.Clamp(thumbX, -Thumb.Width / 2 + 2, rail - Thumb.Width / 2 + 2);
        var thumbY = (Root.ActualHeight - Thumb.Height) / 2;

        if (animateThumb && !_dragging)
        {
            AnimateCanvasLeft(Thumb, thumbX, 120);
        }
        else
        {
            Thumb.BeginAnimation(Canvas.LeftProperty, null);
            Canvas.SetLeft(Thumb, thumbX);
        }

        Canvas.SetTop(Thumb, thumbY);

        FlowBrush.MappingMode = BrushMappingMode.Absolute;
        FlowBrush.SpreadMethod = GradientSpreadMethod.Repeat;
        FlowBrush.StartPoint = new Point(0, 0.5);
        FlowBrush.EndPoint = new Point(FlowBandWidth, 0.5);
        FlowTx.X = -(_flowOffset % FlowBandWidth);
    }

    private static void AnimateCanvasLeft(UIElement el, double to, int ms)
    {
        var from = Canvas.GetLeft(el);
        if (double.IsNaN(from))
        {
            from = to;
        }

        var anim = new DoubleAnimation(from, to, TimeSpan.FromMilliseconds(ms))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut },
            FillBehavior = FillBehavior.Stop,
        };
        anim.Completed += (_, _) => Canvas.SetLeft(el, to);
        el.BeginAnimation(Canvas.LeftProperty, anim);
    }

    private void SyncFlow(bool active)
    {
        if (!IsLoaded)
        {
            return;
        }

        if (active)
        {
            StartFlow();
        }
        else
        {
            StopFlow(immediate: true);
        }
    }

    private void StartFlow()
    {
        // Cancel any pending fade-out Completed that would zero Opacity after reopen.
        _fadeEpoch++;
        ClearOpacityAnims();

        _flowRunning = true;
        FlowLayer.Opacity = 1;
        FlowWash.Opacity = 0.65;
        RailGlow.Opacity = 0.8;
        ThumbHalo.Opacity = 0.85;
        ThumbShadow.Color = Color.FromRgb(0x1A, 0x73, 0xE8);
        ThumbShadow.BlurRadius = 14;
        ThumbShadow.Opacity = 0.5;
        ThumbShadow.ShadowDepth = 0;

        if (_flowTimer is null)
        {
            _flowTimer = new DispatcherTimer(DispatcherPriority.Render)
            {
                Interval = TimeSpan.FromMilliseconds(16),
            };
            _flowTimer.Tick += OnFlowTick;
        }

        if (!_flowTimer.IsEnabled)
        {
            _flowTimer.Start();
        }

        LayoutChrome(animateThumb: false);
    }

    private void OnFlowTick(object? sender, EventArgs e)
    {
        if (!_flowRunning || !FlowActive)
        {
            PauseFlowTimer();
            return;
        }

        // Keep layers visible even if a stale fade tried to hide them.
        if (FlowLayer.Opacity < 0.99)
        {
            ClearOpacityAnims();
            FlowLayer.Opacity = 1;
            FlowWash.Opacity = 0.65;
        }

        _flowOffset += FlowPixelsPerSecond * (16.0 / 1000.0);
        if (_flowOffset >= FlowBandWidth)
        {
            _flowOffset -= FlowBandWidth;
        }

        FlowTx.X = -_flowOffset;

        var breath = 0.5 + 0.5 * Math.Sin((_flowOffset / FlowBandWidth) * Math.PI * 2);
        ThumbHalo.Opacity = 0.45 + 0.5 * breath;
        RailGlow.Opacity = 0.45 + 0.5 * breath;
    }

    private void PauseFlowTimer()
    {
        if (_flowTimer is { IsEnabled: true })
        {
            _flowTimer.Stop();
        }
    }

    private void StopFlow(bool immediate)
    {
        _flowRunning = false;
        PauseFlowTimer();
        FlowTx.X = -(_flowOffset % FlowBandWidth);

        ThumbShadow.Color = Color.FromRgb(0x20, 0x21, 0x24);
        ThumbShadow.BlurRadius = 8;
        ThumbShadow.Opacity = 0.22;
        ThumbShadow.ShadowDepth = 1;
        ThumbHalo.Opacity = 0;

        if (immediate)
        {
            _fadeEpoch++;
            ClearOpacityAnims();
            FlowLayer.Opacity = 0;
            FlowWash.Opacity = 0;
            RailGlow.Opacity = 0;
            return;
        }

        FadeTo(FlowLayer, 0, 160);
        FadeTo(FlowWash, 0, 160);
        FadeTo(RailGlow, 0, 180);
    }

    private void ClearOpacityAnims()
    {
        FlowLayer.BeginAnimation(OpacityProperty, null);
        FlowWash.BeginAnimation(OpacityProperty, null);
        RailGlow.BeginAnimation(OpacityProperty, null);
        ThumbHalo.BeginAnimation(OpacityProperty, null);
    }

    private void FadeTo(UIElement el, double to, int ms)
    {
        var epoch = ++_fadeEpoch;
        ClearOpacityAnims();
        var anim = new DoubleAnimation(el.Opacity, to, TimeSpan.FromMilliseconds(ms))
        {
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut },
            FillBehavior = FillBehavior.Stop,
        };
        anim.Completed += (_, _) =>
        {
            // Ignore completions from a fade that was superseded by StartFlow / reopen.
            if (epoch != _fadeEpoch || _flowRunning)
            {
                return;
            }

            el.Opacity = to;
        };
        el.BeginAnimation(UIElement.OpacityProperty, anim);
    }
}
