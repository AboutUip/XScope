using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using XScope.Services;

namespace XScope;

public partial class SplashWindow : Window
{
    private bool _finished;
    private Storyboard? _storyboard;
    private MainWindow? _warmMain;

    // Final "XScope" slot for X; temporary centered "Xuan" cluster before 归位.
    private double _xHomeX;
    private double _xHomeY;
    private double _xTempX;
    private double _xTempY;

    public SplashWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        ApplySplashTheme();
        Focus();
        LayoutSlots();

        // Start Helix/D3D warmup immediately — splash animation covers the cost.
        HelixGpuWarmup.Begin(Dispatcher);

        if (SystemParameters.ClientAreaAnimation)
        {
            PlayIntro();
        }
        else
        {
            ShowFinalInstant();
            ScheduleFinish(TimeSpan.FromMilliseconds(450));
        }
    }

    private void ApplySplashTheme()
    {
        var dark = ThemeService.IsDarkEffective;
        if (LightWash is not null)
        {
            LightWash.Visibility = dark ? Visibility.Collapsed : Visibility.Visible;
        }

        if (DarkWash is not null)
        {
            DarkWash.Visibility = dark ? Visibility.Visible : Visibility.Collapsed;
        }

        if (LightVeil is not null)
        {
            LightVeil.Visibility = dark ? Visibility.Collapsed : Visibility.Visible;
        }
    }

    private static double GlyphWidth(TextBlock g)
    {
        g.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        return g.ActualWidth > 0.5 ? g.ActualWidth : g.DesiredSize.Width;
    }

    private static double GlyphHeight(TextBlock g)
    {
        g.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        return g.ActualHeight > 0.5 ? g.ActualHeight : g.DesiredSize.Height;
    }

    private void LayoutSlots()
    {
        // Force measure so home slots match rendered widths (fixes X misalignment).
        Stage.UpdateLayout();
        var finals = new[] { GlyphX, GlyphS, GlyphC, GlyphO, GlyphP, GlyphE };
        var widths = finals.Select(GlyphWidth).ToArray();
        var heights = finals.Select(GlyphHeight).ToArray();

        const double tracking = 1.0;
        var total = widths.Sum() + tracking * (widths.Length - 1);
        var cursor = (Stage.Width - total) / 2;
        var baseline = (Stage.Height - heights.Max()) / 2 + 2;

        _xHomeX = cursor;
        _xHomeY = baseline + (heights.Max() - heights[0]) / 2;
        cursor += widths[0] + tracking;
        for (var i = 0; i < 5; i++)
        {
            Canvas.SetLeft(finals[i + 1], cursor);
            Canvas.SetTop(finals[i + 1], baseline + (heights.Max() - heights[i + 1]) / 2);
            cursor += widths[i + 1] + tracking;
        }

        // Xuan cluster: optical center, shared baseline with X home Y family.
        const double xuanTrack = 0.0;
        var uw = GlyphWidth(GlyphU);
        var aw = GlyphWidth(GlyphA);
        var nw = GlyphWidth(GlyphN);
        var xw = widths[0];
        var xuanW = xw + uw + aw + nw + xuanTrack * 3;
        _xTempX = (Stage.Width - xuanW) / 2;
        _xTempY = _xHomeY;
        Canvas.SetLeft(GlyphX, _xTempX);
        Canvas.SetTop(GlyphX, _xTempY);

        var tx = _xTempX + xw + xuanTrack;
        var uy = _xTempY + (heights[0] - GlyphHeight(GlyphU)) / 2;
        Canvas.SetLeft(GlyphU, tx);
        Canvas.SetTop(GlyphU, uy);
        tx += uw + xuanTrack;
        Canvas.SetLeft(GlyphA, tx);
        Canvas.SetTop(GlyphA, _xTempY + (heights[0] - GlyphHeight(GlyphA)) / 2);
        tx += aw + xuanTrack;
        Canvas.SetLeft(GlyphN, tx);
        Canvas.SetTop(GlyphN, _xTempY + (heights[0] - GlyphHeight(GlyphN)) / 2);
    }

    private void PlayIntro()
    {
        var sb = new Storyboard();
        _storyboard = sb;

        AddBloom(sb);
        AddMotes(sb);

        // Curve beats (not uniform slow):
        //   punch in → soft cascade → short hold → snap fold → lively Scope.
        // ~3.6s total energy, with one breath at the Xuan crest.

        // A1 — brand stem X: dedicated first entrance (not the generic Scope roll).
        EnterXFirst(sb, beginMs: 120);

        // A2 — X invites the name (anticipation), then u-a-n cascade from the right.
        AddXInvite(sb, beginMs: 720);
        EnterXuanJoin(sb, GlyphU, GlyphUTranslate, GlyphURotate, GlyphUScale, beginMs: 790);
        EnterXuanJoin(sb, GlyphA, GlyphATranslate, GlyphARotate, GlyphAScale, beginMs: 930);
        EnterXuanJoin(sb, GlyphN, GlyphNTranslate, GlyphNRotate, GlyphNScale, beginMs: 1070);

        // A3 — one short living crest (readable "Xuan"), not a long idle.
        AddBreath(sb, GlyphXTranslate, beginMs: 1380, cycles: 1, amp: 2.4, stepMs: 110);
        AddBreath(sb, GlyphUTranslate, beginMs: 1400, cycles: 1, amp: 3.0, stepMs: 110);
        AddBreath(sb, GlyphATranslate, beginMs: 1420, cycles: 1, amp: 2.6, stepMs: 110);
        AddBreath(sb, GlyphNTranslate, beginMs: 1440, cycles: 1, amp: 3.2, stepMs: 110);

        // B — fold name into X (fast cascade), then hard-home X into XScope slot.
        const int absorbAt = 1650;
        AddAbsorb(sb, GlyphN, GlyphNTranslate, GlyphNRotate, GlyphNScale, absorbAt,
            diveX: _xTempX - Canvas.GetLeft(GlyphN) + 2,
            diveY: _xTempY - Canvas.GetTop(GlyphN));
        AddAbsorb(sb, GlyphA, GlyphATranslate, GlyphARotate, GlyphAScale, absorbAt + 70,
            diveX: _xTempX - Canvas.GetLeft(GlyphA) + 1,
            diveY: _xTempY - Canvas.GetTop(GlyphA));
        AddAbsorb(sb, GlyphU, GlyphUTranslate, GlyphURotate, GlyphUScale, absorbAt + 140,
            diveX: _xTempX - Canvas.GetLeft(GlyphU),
            diveY: _xTempY - Canvas.GetTop(GlyphU));

        AddXImpactAndHome(sb, beginMs: absorbAt + 100);

        // C — Scope: accelerating stagger (tightening rhythm).
        const int scopeAt = 2150;
        EnterRollFromSide(sb, GlyphS, GlyphSTranslate, GlyphSRotate, GlyphSScale,
            beginMs: scopeAt, fromLeft: true);
        EnterJumpFromSide(sb, GlyphC, GlyphCTranslate, GlyphCRotate, GlyphCScale,
            beginMs: scopeAt + 85, fromLeft: false);
        EnterJumpFromSide(sb, GlyphO, GlyphOTranslate, GlyphORotate, GlyphOScale,
            beginMs: scopeAt + 155, fromLeft: true);
        EnterRollFromSide(sb, GlyphP, GlyphPTranslate, GlyphPRotate, GlyphPScale,
            beginMs: scopeAt + 215, fromLeft: false);
        EnterJumpFromSide(sb, GlyphE, GlyphETranslate, GlyphERotate, GlyphEScale,
            beginMs: scopeAt + 265, fromLeft: true);

        // Settle Scope only — never nudge X off its locked home.
        AddStageSettle(sb, beginMs: scopeAt + 620);
        // Soft second beat after Scope settles — fade/rise, not a hard slide-in.
        const int taglineAt = scopeAt + 720;
        EnterTaglineSoft(sb, beginMs: taglineAt);

        // Hold until tagline finishes easing in, then fade out.
        const int fadeAt = taglineAt + 780;
        const int fadeMs = 300;
        var rootFade = new DoubleAnimation(1, 0, TimeSpan.FromMilliseconds(fadeMs))
        {
            BeginTime = TimeSpan.FromMilliseconds(fadeAt),
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseIn },
            FillBehavior = FillBehavior.HoldEnd // keep 0 — Stop would flash splash back to opaque
        };
        Storyboard.SetTarget(rootFade, RootChrome);
        Storyboard.SetTargetProperty(rootFade, new PropertyPath(OpacityProperty));
        sb.Children.Add(rootFade);

        // Prefetch main window during the last beat so Show() is instant after fade.
        ScheduleMainWarmup(TimeSpan.FromMilliseconds(Math.Max(0, fadeAt - 400)));
        ScheduleFinish(TimeSpan.FromMilliseconds(fadeAt + fadeMs));
        sb.Begin();
    }

    private void AddBloom(Storyboard sb)
    {
        var op = new DoubleAnimationUsingKeyFrames
        {
            BeginTime = TimeSpan.FromMilliseconds(40)
        };
        op.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        op.KeyFrames.Add(new EasingDoubleKeyFrame(0.8, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(700)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        op.KeyFrames.Add(new EasingDoubleKeyFrame(0.32, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(2800)),
            new SineEase { EasingMode = EasingMode.EaseInOut }));
        op.KeyFrames.Add(new EasingDoubleKeyFrame(0.5, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(4200)),
            new SineEase { EasingMode = EasingMode.EaseInOut }));
        Storyboard.SetTarget(op, InkBloom);
        Storyboard.SetTargetProperty(op, new PropertyPath(OpacityProperty));
        sb.Children.Add(op);

        var sx = new DoubleAnimation(0.35, 1.15, TimeSpan.FromMilliseconds(1400))
        {
            BeginTime = TimeSpan.FromMilliseconds(40),
            EasingFunction = new QuinticEase { EasingMode = EasingMode.EaseOut }
        };
        Storyboard.SetTarget(sx, InkBloomScale);
        Storyboard.SetTargetProperty(sx, new PropertyPath(ScaleTransform.ScaleXProperty));
        sb.Children.Add(sx);
        var sy = sx.Clone();
        Storyboard.SetTarget(sy, InkBloomScale);
        Storyboard.SetTargetProperty(sy, new PropertyPath(ScaleTransform.ScaleYProperty));
        sb.Children.Add(sy);
    }

    private void AddMotes(Storyboard sb)
    {
        AnimateMote(sb, Mote0, 200, dy: -48, dx: 18, life: 2400);
        AnimateMote(sb, Mote1, 480, dy: 36, dx: -22, life: 2800);
        AnimateMote(sb, Mote2, 900, dy: -30, dx: 28, life: 2600);
        AnimateMote(sb, Mote3, 1600, dy: 40, dx: -16, life: 3000);
        AnimateMote(sb, Mote4, 2400, dy: -55, dx: 8, life: 3200);
    }

    private static void AnimateMote(Storyboard sb, Ellipse mote, int beginMs, double dy, double dx, int life)
    {
        var op = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        op.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        op.KeyFrames.Add(new EasingDoubleKeyFrame(0.7, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(220)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        op.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(life)),
            new QuadraticEase { EasingMode = EasingMode.EaseIn }));
        Storyboard.SetTarget(op, mote);
        Storyboard.SetTargetProperty(op, new PropertyPath(OpacityProperty));
        sb.Children.Add(op);

        var left = Canvas.GetLeft(mote);
        var top = Canvas.GetTop(mote);
        var ax = new DoubleAnimation(left, left + dx, TimeSpan.FromMilliseconds(life))
        {
            BeginTime = TimeSpan.FromMilliseconds(beginMs),
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
        };
        Storyboard.SetTarget(ax, mote);
        Storyboard.SetTargetProperty(ax, new PropertyPath(Canvas.LeftProperty));
        sb.Children.Add(ax);

        var ay = new DoubleAnimation(top, top + dy, TimeSpan.FromMilliseconds(life))
        {
            BeginTime = TimeSpan.FromMilliseconds(beginMs),
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseOut }
        };
        Storyboard.SetTarget(ay, mote);
        Storyboard.SetTargetProperty(ay, new PropertyPath(Canvas.TopProperty));
        sb.Children.Add(ay);
    }

    /// <summary>
    /// First X: long ease-in from the left, three-quarter roll, weighted plant.
    /// Feels like the brand arriving — not a Scope letter scrambling in.
    /// </summary>
    private void EnterXFirst(Storyboard sb, int beginMs)
    {
        const int dur = 620;
        const double fromX = -320;

        RevealOffstage(sb, GlyphX, beginMs);

        // Drift in with ease-in (soft stage edge), then plant with a short overshoot.
        var x = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        x.KeyFrames.Add(new DiscreteDoubleKeyFrame(fromX, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(fromX * 0.62, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.4)),
            new CubicEase { EasingMode = EasingMode.EaseIn }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(10, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.78)),
            new CubicEase { EasingMode = EasingMode.EaseOut }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.45 }));
        Storyboard.SetTarget(x, GlyphXTranslate);
        Storyboard.SetTargetProperty(x, new PropertyPath(TranslateTransform.XProperty));
        sb.Children.Add(x);

        // Low glide → settle (less bounce than Scope hops).
        var y = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        y.KeyFrames.Add(new DiscreteDoubleKeyFrame(6, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(-3, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.55)),
            new SineEase { EasingMode = EasingMode.EaseOut }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        Storyboard.SetTarget(y, GlyphXTranslate);
        Storyboard.SetTargetProperty(y, new PropertyPath(TranslateTransform.YProperty));
        sb.Children.Add(y);

        // 270° roll (readable as tumbling in, not a full dizzy spin).
        var rot = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        rot.KeyFrames.Add(new DiscreteDoubleKeyFrame(-270, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(-40, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.7)),
            new CubicEase { EasingMode = EasingMode.EaseInOut }));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.35 }));
        Storyboard.SetTarget(rot, GlyphXRotate);
        Storyboard.SetTargetProperty(rot, new PropertyPath(RotateTransform.AngleProperty));
        sb.Children.Add(rot);

        // Weighted squash on plant — brand stem lands with mass.
        var sx = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sx.KeyFrames.Add(new DiscreteDoubleKeyFrame(0.88, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(0.95, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.55)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1.12, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.82)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.3 }));
        Storyboard.SetTarget(sx, GlyphXScale);
        Storyboard.SetTargetProperty(sx, new PropertyPath(ScaleTransform.ScaleXProperty));
        sb.Children.Add(sx);

        var sy = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sy.KeyFrames.Add(new DiscreteDoubleKeyFrame(0.75, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1.05, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.55)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(0.9, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.82)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.3 }));
        Storyboard.SetTarget(sy, GlyphXScale);
        Storyboard.SetTargetProperty(sy, new PropertyPath(ScaleTransform.ScaleYProperty));
        sb.Children.Add(sy);
    }

    /// <summary>Letter is visible off-stage immediately, then rolls in from L/R — no pop/fade-in.</summary>
    private static void EnterRollFromSide(Storyboard sb, UIElement glyph, TranslateTransform translate,
        RotateTransform rotate, ScaleTransform scale, int beginMs, bool fromLeft, bool slow = false)
    {
        var dur = slow ? 720 : 540;
        var fromX = fromLeft ? -300.0 : 300.0;
        var spin = fromLeft ? -360.0 : 360.0;

        RevealOffstage(sb, glyph, beginMs);

        // Ease-in onto stage (soft edge), then lively settle — avoids abrupt "appear".
        var x = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        x.KeyFrames.Add(new DiscreteDoubleKeyFrame(fromX, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(fromX * 0.5, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.32)),
            new CubicEase { EasingMode = EasingMode.EaseIn }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(fromLeft ? 7 : -7,
            KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.78)),
            new CubicEase { EasingMode = EasingMode.EaseOut }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 5 }));
        Storyboard.SetTarget(x, translate);
        Storyboard.SetTargetProperty(x, new PropertyPath(TranslateTransform.XProperty));
        sb.Children.Add(x);

        var y = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        y.KeyFrames.Add(new DiscreteDoubleKeyFrame(8, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(-5, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.48)),
            new SineEase { EasingMode = EasingMode.EaseOut }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        Storyboard.SetTarget(y, translate);
        Storyboard.SetTargetProperty(y, new PropertyPath(TranslateTransform.YProperty));
        sb.Children.Add(y);

        var rot = new DoubleAnimation(spin, 0, TimeSpan.FromMilliseconds(dur))
        {
            BeginTime = TimeSpan.FromMilliseconds(beginMs),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
        Storyboard.SetTarget(rot, rotate);
        Storyboard.SetTargetProperty(rot, new PropertyPath(RotateTransform.AngleProperty));
        sb.Children.Add(rot);

        var sx = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sx.KeyFrames.Add(new DiscreteDoubleKeyFrame(0.92, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1.06, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.55)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 5 }));
        Storyboard.SetTarget(sx, scale);
        Storyboard.SetTargetProperty(sx, new PropertyPath(ScaleTransform.ScaleXProperty));
        sb.Children.Add(sx);
        var sy = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sy.KeyFrames.Add(new DiscreteDoubleKeyFrame(0.8, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1.1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.55)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 5 }));
        Storyboard.SetTarget(sy, scale);
        Storyboard.SetTargetProperty(sy, new PropertyPath(ScaleTransform.ScaleYProperty));
        sb.Children.Add(sy);
    }

    /// <summary>X leans right and pulses — calls the Xuan letters in (anticipation, not abrupt).</summary>
    private void AddXInvite(Storyboard sb, int beginMs)
    {
        var lean = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        lean.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        lean.KeyFrames.Add(new EasingDoubleKeyFrame(5, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(90)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        lean.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(220)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.4 }));
        Storyboard.SetTarget(lean, GlyphXTranslate);
        Storyboard.SetTargetProperty(lean, new PropertyPath(TranslateTransform.XProperty));
        sb.Children.Add(lean);

        var sx = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sx.KeyFrames.Add(new DiscreteDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1.08, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(100)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(240)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 5 }));
        Storyboard.SetTarget(sx, GlyphXScale);
        Storyboard.SetTargetProperty(sx, new PropertyPath(ScaleTransform.ScaleXProperty));
        sb.Children.Add(sx);
        var sy = sx.Clone();
        Storyboard.SetTarget(sy, GlyphXScale);
        Storyboard.SetTargetProperty(sy, new PropertyPath(ScaleTransform.ScaleYProperty));
        sb.Children.Add(sy);
    }

    /// <summary>
    /// Xuan u/a/n: ease-in from deep off-stage (no hard edge pop), half-roll + soft hop.
    /// </summary>
    private static void EnterXuanJoin(Storyboard sb, UIElement glyph, TranslateTransform translate,
        RotateTransform rotate, ScaleTransform scale, int beginMs)
    {
        const int dur = 520;
        const double fromX = 300; // deeper start → motion begins before the letter is readable

        RevealOffstage(sb, glyph, beginMs);

        // Ease-in for first half (accelerate onto stage), ease-out settle — curve, not linear crawl.
        var x = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        x.KeyFrames.Add(new DiscreteDoubleKeyFrame(fromX, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(fromX * 0.55, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.35)),
            new CubicEase { EasingMode = EasingMode.EaseIn }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(-3, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.78)),
            new CubicEase { EasingMode = EasingMode.EaseOut }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.35 }));
        Storyboard.SetTarget(x, translate);
        Storyboard.SetTargetProperty(x, new PropertyPath(TranslateTransform.XProperty));
        sb.Children.Add(x);

        var y = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        y.KeyFrames.Add(new DiscreteDoubleKeyFrame(10, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(-14, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.42)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(2, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.75)),
            new QuadraticEase { EasingMode = EasingMode.EaseIn }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 5 }));
        Storyboard.SetTarget(y, translate);
        Storyboard.SetTargetProperty(y, new PropertyPath(TranslateTransform.YProperty));
        sb.Children.Add(y);

        var rot = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        rot.KeyFrames.Add(new DiscreteDoubleKeyFrame(200, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(40, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.55)),
            new CubicEase { EasingMode = EasingMode.EaseIn }));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.4 }));
        Storyboard.SetTarget(rot, rotate);
        Storyboard.SetTargetProperty(rot, new PropertyPath(RotateTransform.AngleProperty));
        sb.Children.Add(rot);

        var sx = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sx.KeyFrames.Add(new DiscreteDoubleKeyFrame(0.82, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1.1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.5)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 4 }));
        Storyboard.SetTarget(sx, scale);
        Storyboard.SetTargetProperty(sx, new PropertyPath(ScaleTransform.ScaleXProperty));
        sb.Children.Add(sx);
        var sy = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sy.KeyFrames.Add(new DiscreteDoubleKeyFrame(0.78, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1.12, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.5)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 4 }));
        Storyboard.SetTarget(sy, scale);
        Storyboard.SetTargetProperty(sy, new PropertyPath(ScaleTransform.ScaleYProperty));
        sb.Children.Add(sy);
    }

    /// <summary>Letter hops in from a side with airborne arcs — no pop/fade-in.</summary>
    private static void EnterJumpFromSide(Storyboard sb, UIElement glyph, TranslateTransform translate,
        RotateTransform rotate, ScaleTransform scale, int beginMs, bool fromLeft, bool slow = false)
    {
        var dur = slow ? 820 : 600;
        var fromX = fromLeft ? -260.0 : 260.0;
        var tilt = fromLeft ? -24.0 : 24.0;

        RevealOffstage(sb, glyph, beginMs);

        var x = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        x.KeyFrames.Add(new DiscreteDoubleKeyFrame(fromX, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(fromX * 0.42,
            KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.38)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(fromLeft ? 8 : -8,
            KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.78)),
            new QuadraticEase { EasingMode = EasingMode.EaseInOut }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = slow ? 1 : 2, Springiness = 6 }));
        Storyboard.SetTarget(x, translate);
        Storyboard.SetTargetProperty(x, new PropertyPath(TranslateTransform.XProperty));
        sb.Children.Add(x);

        var y = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        y.KeyFrames.Add(new DiscreteDoubleKeyFrame(16, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(-26, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.3)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(5, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.5)),
            new QuadraticEase { EasingMode = EasingMode.EaseIn }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(-14, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.7)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(3, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.86)),
            new QuadraticEase { EasingMode = EasingMode.EaseIn }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new BounceEase { EasingMode = EasingMode.EaseOut, Bounces = 2, Bounciness = 2.2 }));
        Storyboard.SetTarget(y, translate);
        Storyboard.SetTargetProperty(y, new PropertyPath(TranslateTransform.YProperty));
        sb.Children.Add(y);

        var rot = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        rot.KeyFrames.Add(new DiscreteDoubleKeyFrame(tilt, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(tilt * -0.55,
            KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.42)),
            new QuadraticEase { EasingMode = EasingMode.EaseInOut }));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(tilt * 0.2,
            KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.72)),
            new QuadraticEase { EasingMode = EasingMode.EaseInOut }));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.45 }));
        Storyboard.SetTarget(rot, rotate);
        Storyboard.SetTargetProperty(rot, new PropertyPath(RotateTransform.AngleProperty));
        sb.Children.Add(rot);

        var sx = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sx.KeyFrames.Add(new DiscreteDoubleKeyFrame(0.88, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1.04, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.32)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(0.92, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.52)),
            new QuadraticEase { EasingMode = EasingMode.EaseIn }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1.06, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.74)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 5 }));
        Storyboard.SetTarget(sx, scale);
        Storyboard.SetTargetProperty(sx, new PropertyPath(ScaleTransform.ScaleXProperty));
        sb.Children.Add(sx);
        var sy = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sy.KeyFrames.Add(new DiscreteDoubleKeyFrame(1.12, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(0.9, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.32)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1.1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.52)),
            new QuadraticEase { EasingMode = EasingMode.EaseIn }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(0.94, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur * 0.74)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(dur)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 5 }));
        Storyboard.SetTarget(sy, scale);
        Storyboard.SetTargetProperty(sy, new PropertyPath(ScaleTransform.ScaleYProperty));
        sb.Children.Add(sy);
    }

    /// <summary>
    /// Tagline: slow opacity + gentle rise. Instant opacity=1 felt abrupt under the wordmark.
    /// </summary>
    private void EnterTaglineSoft(Storyboard sb, int beginMs)
    {
        const int fadeMs = 720;

        var opacity = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        opacity.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        opacity.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(fadeMs)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        Storyboard.SetTarget(opacity, Tagline);
        Storyboard.SetTargetProperty(opacity, new PropertyPath(OpacityProperty));
        sb.Children.Add(opacity);

        var y = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        y.KeyFrames.Add(new DiscreteDoubleKeyFrame(10, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(fadeMs)),
            new CubicEase { EasingMode = EasingMode.EaseOut }));
        Storyboard.SetTarget(y, TaglineTranslate);
        Storyboard.SetTargetProperty(y, new PropertyPath(TranslateTransform.YProperty));
        sb.Children.Add(y);

        // Keep X locked — no side pop.
        HoldZero(sb, TaglineTranslate, TranslateTransform.XProperty, beginMs);
    }

    private static void RevealOffstage(Storyboard sb, UIElement glyph, int beginMs)
    {
        var opacity = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        opacity.KeyFrames.Add(new DiscreteDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        Storyboard.SetTarget(opacity, glyph);
        Storyboard.SetTargetProperty(opacity, new PropertyPath(OpacityProperty));
        sb.Children.Add(opacity);
    }

    private static void AddBreath(Storyboard sb, TranslateTransform translate, int beginMs, int cycles,
        double amp, int stepMs = 140)
    {
        var y = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        var t = 0.0;
        y.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        for (var i = 0; i < cycles; i++)
        {
            t += stepMs;
            y.KeyFrames.Add(new EasingDoubleKeyFrame(-amp, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(t)),
                new SineEase { EasingMode = EasingMode.EaseInOut }));
            t += stepMs;
            y.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(t)),
                new SineEase { EasingMode = EasingMode.EaseInOut }));
        }
        Storyboard.SetTarget(y, translate);
        Storyboard.SetTargetProperty(y, new PropertyPath(TranslateTransform.YProperty));
        sb.Children.Add(y);
    }

    private static void AddAbsorb(Storyboard sb, UIElement glyph, TranslateTransform translate,
        RotateTransform rotate, ScaleTransform scale, int beginMs, double diveX, double diveY,
        bool slow = false)
    {
        var travel = slow ? 480 : 340;
        var op = new DoubleAnimation(1, 0, TimeSpan.FromMilliseconds(slow ? 420 : 320))
        {
            BeginTime = TimeSpan.FromMilliseconds(beginMs + (slow ? 80 : 40)),
            EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseIn }
        };
        Storyboard.SetTarget(op, glyph);
        Storyboard.SetTargetProperty(op, new PropertyPath(OpacityProperty));
        sb.Children.Add(op);

        var x = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        x.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(diveX * 0.12, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(slow ? 140 : 90)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        x.KeyFrames.Add(new EasingDoubleKeyFrame(diveX, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(travel)),
            new CubicEase { EasingMode = EasingMode.EaseIn }));
        Storyboard.SetTarget(x, translate);
        Storyboard.SetTargetProperty(x, new PropertyPath(TranslateTransform.XProperty));
        sb.Children.Add(x);

        var y = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        y.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(-8, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(slow ? 150 : 100)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        y.KeyFrames.Add(new EasingDoubleKeyFrame(diveY, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(travel)),
            new CubicEase { EasingMode = EasingMode.EaseIn }));
        Storyboard.SetTarget(y, translate);
        Storyboard.SetTargetProperty(y, new PropertyPath(TranslateTransform.YProperty));
        sb.Children.Add(y);

        var rot = new DoubleAnimation(0, diveX < 0 ? -40 : 40, TimeSpan.FromMilliseconds(travel))
        {
            BeginTime = TimeSpan.FromMilliseconds(beginMs),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn }
        };
        Storyboard.SetTarget(rot, rotate);
        Storyboard.SetTargetProperty(rot, new PropertyPath(RotateTransform.AngleProperty));
        sb.Children.Add(rot);

        var sx = new DoubleAnimation(1, 0.12, TimeSpan.FromMilliseconds(travel))
        {
            BeginTime = TimeSpan.FromMilliseconds(beginMs),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn }
        };
        Storyboard.SetTarget(sx, scale);
        Storyboard.SetTargetProperty(sx, new PropertyPath(ScaleTransform.ScaleXProperty));
        sb.Children.Add(sx);
        var sy = new DoubleAnimation(1, 0.12, TimeSpan.FromMilliseconds(travel))
        {
            BeginTime = TimeSpan.FromMilliseconds(beginMs),
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn }
        };
        Storyboard.SetTarget(sy, scale);
        Storyboard.SetTargetProperty(sy, new PropertyPath(ScaleTransform.ScaleYProperty));
        sb.Children.Add(sy);
    }

    private void AddXImpactAndHome(Storyboard sb, int beginMs)
    {
        // Zero residual transforms first — breath/invite must not pollute 归位 math.
        HoldZero(sb, GlyphXTranslate, TranslateTransform.XProperty, beginMs);
        HoldZero(sb, GlyphXTranslate, TranslateTransform.YProperty, beginMs);
        HoldZero(sb, GlyphXRotate, RotateTransform.AngleProperty, beginMs);

        // Impact squash at temp, then slide Canvas to exact home and lock.
        var sx = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sx.KeyFrames.Add(new DiscreteDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1.2, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(70)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(0.88, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(140)),
            new QuadraticEase { EasingMode = EasingMode.EaseIn }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1.05, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(260)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sx.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(400)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 2, Springiness = 5 }));
        Storyboard.SetTarget(sx, GlyphXScale);
        Storyboard.SetTargetProperty(sx, new PropertyPath(ScaleTransform.ScaleXProperty));
        sb.Children.Add(sx);

        var sy = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        sy.KeyFrames.Add(new DiscreteDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(0.78, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(70)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1.16, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(140)),
            new QuadraticEase { EasingMode = EasingMode.EaseIn }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(0.96, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(260)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        sy.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(400)),
            new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 2, Springiness = 5 }));
        Storyboard.SetTarget(sy, GlyphXScale);
        Storyboard.SetTargetProperty(sy, new PropertyPath(ScaleTransform.ScaleYProperty));
        sb.Children.Add(sy);

        var rot = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        rot.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(-5, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(80)),
            new QuadraticEase { EasingMode = EasingMode.EaseOut }));
        rot.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(280)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.45 }));
        Storyboard.SetTarget(rot, GlyphXRotate);
        Storyboard.SetTargetProperty(rot, new PropertyPath(RotateTransform.AngleProperty));
        sb.Children.Add(rot);

        // 归位 slide: Canvas only (transforms stay identity) → exact XScope slot.
        const int homeAt = 120;
        var left = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs + homeAt) };
        left.KeyFrames.Add(new DiscreteDoubleKeyFrame(_xTempX, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        left.KeyFrames.Add(new EasingDoubleKeyFrame(_xHomeX - 5, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(220)),
            new CubicEase { EasingMode = EasingMode.EaseInOut }));
        left.KeyFrames.Add(new EasingDoubleKeyFrame(_xHomeX, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(380)),
            new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.35 }));
        Storyboard.SetTarget(left, GlyphX);
        Storyboard.SetTargetProperty(left, new PropertyPath(Canvas.LeftProperty));
        sb.Children.Add(left);

        var top = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs + homeAt) };
        top.KeyFrames.Add(new DiscreteDoubleKeyFrame(_xTempY, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        top.KeyFrames.Add(new EasingDoubleKeyFrame(_xHomeY, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(300)),
            new CubicEase { EasingMode = EasingMode.EaseOut }));
        Storyboard.SetTarget(top, GlyphX);
        Storyboard.SetTargetProperty(top, new PropertyPath(Canvas.TopProperty));
        sb.Children.Add(top);

        // Snap residual translate to 0 (no long hold clocks — those delayed main-window handoff).
        HoldZero(sb, GlyphXTranslate, TranslateTransform.XProperty, beginMs + homeAt);
        HoldZero(sb, GlyphXTranslate, TranslateTransform.YProperty, beginMs + homeAt);
    }

    private static void HoldZero(Storyboard sb, DependencyObject target, DependencyProperty prop, int beginMs)
    {
        var anim = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
        anim.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
        Storyboard.SetTarget(anim, target);
        Storyboard.SetTargetProperty(anim, new PropertyPath(prop));
        sb.Children.Add(anim);
    }

    private void AddStageSettle(Storyboard sb, int beginMs)
    {
        // Scope letters only — X stays on its locked home slot.
        foreach (var t in new[] { GlyphSTranslate, GlyphCTranslate, GlyphOTranslate, GlyphPTranslate, GlyphETranslate })
        {
            var y = new DoubleAnimationUsingKeyFrames { BeginTime = TimeSpan.FromMilliseconds(beginMs) };
            y.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.Zero)));
            y.KeyFrames.Add(new EasingDoubleKeyFrame(3.0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(110)),
                new SineEase { EasingMode = EasingMode.EaseIn }));
            y.KeyFrames.Add(new EasingDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(300)),
                new ElasticEase { EasingMode = EasingMode.EaseOut, Oscillations = 1, Springiness = 4 }));
            Storyboard.SetTarget(y, t);
            Storyboard.SetTargetProperty(y, new PropertyPath(TranslateTransform.YProperty));
            sb.Children.Add(y);
        }
    }

    private void ShowFinalInstant()
    {
        Canvas.SetLeft(GlyphX, _xHomeX);
        Canvas.SetTop(GlyphX, _xHomeY);
        GlyphX.Opacity = 1;
        GlyphXTranslate.X = 0;
        GlyphXTranslate.Y = 0;
        GlyphXScale.ScaleX = 1;
        GlyphXScale.ScaleY = 1;
        GlyphXRotate.Angle = 0;
        GlyphU.Opacity = 0;
        GlyphA.Opacity = 0;
        GlyphN.Opacity = 0;
        foreach (var g in new[] { GlyphS, GlyphC, GlyphO, GlyphP, GlyphE })
        {
            g.Opacity = 1;
        }
        GlyphSTranslate.Y = GlyphCTranslate.Y = GlyphOTranslate.Y = GlyphPTranslate.Y = GlyphETranslate.Y = 0;
        Tagline.Opacity = 1;
        TaglineTranslate.Y = 0;
        InkBloom.Opacity = 0.4;
        InkBloomScale.ScaleX = InkBloomScale.ScaleY = 1;
    }

    private void ScheduleMainWarmup(TimeSpan delay)
    {
        var timer = new System.Windows.Threading.DispatcherTimer { Interval = delay };
        timer.Tick += (_, _) =>
        {
            timer.Stop();
            if (_finished || _warmMain is not null)
            {
                return;
            }
            // Construct + parse XAML now; Show happens in Finish().
            _warmMain = new MainWindow();
            // Second nudge if first idle pass was skipped / still running.
            HelixGpuWarmup.Begin(Dispatcher);
        };
        timer.Start();
    }

    private void ScheduleFinish(TimeSpan delay)
    {
        var timer = new System.Windows.Threading.DispatcherTimer { Interval = delay };
        timer.Tick += (_, _) =>
        {
            timer.Stop();
            Finish();
        };
        timer.Start();
    }

    private void OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        // Intentional skip only — mouse clicks must not dismiss the intro
        // (users often click the splash just to focus / bring it forward).
        if (e.Key is Key.Escape or Key.Enter or Key.Space)
        {
            Skip();
            e.Handled = true;
        }
    }

    private void Skip()
    {
        if (_finished)
        {
            return;
        }
        Finish();
    }

    private void Finish()
    {
        if (_finished)
        {
            return;
        }
        _finished = true;

        // Kill opacity animations and force invisible BEFORE Stop() —
        // Stop()/FillBehavior.Stop would otherwise restore Opacity=1 and flash the splash.
        RootChrome.BeginAnimation(UIElement.OpacityProperty, null);
        BeginAnimation(OpacityProperty, null);
        RootChrome.Opacity = 0;
        Opacity = 0;
        Hide();

        var sb = _storyboard;
        _storyboard = null;
        sb?.Stop();

        var main = _warmMain ?? new MainWindow();
        _warmMain = null;
        Application.Current.MainWindow = main;
        Application.Current.ShutdownMode = ShutdownMode.OnMainWindowClose;
        main.Show();
        main.Activate();
        Close();
    }

    protected override void OnClosed(EventArgs e)
    {
        base.OnClosed(e);
        if (!_finished)
        {
            Application.Current.Shutdown();
        }
    }
}
