using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using XScope.ViewModels;

namespace XScope.Controls;

public partial class ResearchSidePanel : UserControl
{
    public const double CollapsedWidth = 52;
    public const double DefaultExpandedWidth = 400;
    public const double MinExpandedWidth = 240;

    private ResearchSidePanelViewModel? _vm;
    private Storyboard? _activeStoryboard;
    private double _userExpandedWidth = DefaultExpandedWidth;
    private bool _resizing;
    private Point _resizeStart;
    private double _resizeStartWidth;

    public ResearchSidePanel()
    {
        InitializeComponent();
        Width = CollapsedWidth;
        MinWidth = CollapsedWidth;
        // No hard max — clamp only against parent remaining space while dragging.
        MaxWidth = double.PositiveInfinity;
        ClipToBounds = true;
        DataContextChanged += OnDataContextChanged;
        Loaded += (_, _) => ApplyExpandedState(animate: false);
    }

    private void OnDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (_vm is not null)
        {
            _vm.ExpandedChanged -= OnExpandedChanged;
        }

        _vm = e.NewValue as ResearchSidePanelViewModel;
        if (_vm is not null)
        {
            _vm.ExpandedChanged += OnExpandedChanged;
        }

        ApplyExpandedState(animate: false);
    }

    private void OnExpandedChanged() => ApplyExpandedState(animate: IsLoaded);

    private double MaxAllowedWidth()
    {
        // Leave at least ~320px for the research column.
        if (Parent is FrameworkElement parent && parent.ActualWidth > 400)
        {
            return Math.Max(MinExpandedWidth, parent.ActualWidth - 320);
        }

        return 2400;
    }

    private double ClampExpandedWidth(double w) =>
        Math.Clamp(w, MinExpandedWidth, MaxAllowedWidth());

    private void ApplyExpandedState(bool animate)
    {
        var expanded = _vm?.IsExpanded ?? true;
        var target = expanded ? ClampExpandedWidth(_userExpandedWidth) : CollapsedWidth;

        if (ResizeGrip is not null)
        {
            ResizeGrip.Visibility = expanded ? Visibility.Visible : Visibility.Collapsed;
            ResizeGrip.IsHitTestVisible = expanded;
        }

        if (CollapsedRail is not null)
        {
            CollapsedRail.IsHitTestVisible = !expanded;
            CollapsedRail.Visibility = Visibility.Visible;
        }

        if (ExpandedPanel is not null)
        {
            ExpandedPanel.IsHitTestVisible = expanded;
            ExpandedPanel.Visibility = Visibility.Visible;
        }

        _activeStoryboard?.Stop();
        _activeStoryboard = null;

        if (!animate)
        {
            Width = target;
            MinWidth = expanded ? MinExpandedWidth : CollapsedWidth;
            if (CollapsedRail is not null)
            {
                CollapsedRail.Opacity = expanded ? 0 : 1;
            }

            if (ExpandedPanel is not null)
            {
                ExpandedPanel.Opacity = expanded ? 1 : 0;
                ExpandedPanel.RenderTransform = new TranslateTransform(0, 0);
            }

            return;
        }

        var duration = TimeSpan.FromMilliseconds(280);
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };

        var sb = new Storyboard();
        var widthAnim = new DoubleAnimation(Width, target, duration) { EasingFunction = ease };
        Storyboard.SetTarget(widthAnim, this);
        Storyboard.SetTargetProperty(widthAnim, new PropertyPath(WidthProperty));
        sb.Children.Add(widthAnim);

        var minTarget = expanded ? MinExpandedWidth : CollapsedWidth;
        var minAnim = new DoubleAnimation(MinWidth, minTarget, duration) { EasingFunction = ease };
        Storyboard.SetTarget(minAnim, this);
        Storyboard.SetTargetProperty(minAnim, new PropertyPath(MinWidthProperty));
        sb.Children.Add(minAnim);

        if (CollapsedRail is not null)
        {
            var railOpacity = new DoubleAnimation(CollapsedRail.Opacity, expanded ? 0 : 1, duration)
            {
                EasingFunction = ease,
            };
            Storyboard.SetTarget(railOpacity, CollapsedRail);
            Storyboard.SetTargetProperty(railOpacity, new PropertyPath(OpacityProperty));
            sb.Children.Add(railOpacity);
        }

        if (ExpandedPanel is not null)
        {
            if (ExpandedPanel.RenderTransform is not TranslateTransform)
            {
                ExpandedPanel.RenderTransform = new TranslateTransform(expanded ? 24 : 0, 0);
            }

            var tx = (TranslateTransform)ExpandedPanel.RenderTransform;
            DoubleAnimation slide;
            if (!expanded)
            {
                slide = new DoubleAnimation(0, 16, duration) { EasingFunction = ease };
            }
            else
            {
                tx.X = 20;
                slide = new DoubleAnimation(20, 0, duration) { EasingFunction = ease };
            }

            Storyboard.SetTarget(slide, ExpandedPanel);
            Storyboard.SetTargetProperty(slide, new PropertyPath("(UIElement.RenderTransform).(TranslateTransform.X)"));
            sb.Children.Add(slide);

            var panelOpacity = new DoubleAnimation(ExpandedPanel.Opacity, expanded ? 1 : 0, duration)
            {
                EasingFunction = ease,
            };
            Storyboard.SetTarget(panelOpacity, ExpandedPanel);
            Storyboard.SetTargetProperty(panelOpacity, new PropertyPath(OpacityProperty));
            sb.Children.Add(panelOpacity);
        }

        if (ExpandChevron is not null && !expanded)
        {
            ExpandChevron.RenderTransformOrigin = new Point(0.5, 0.5);
            ExpandChevron.RenderTransform = new RotateTransform(0);
            var spin = new DoubleAnimation(0, -180, TimeSpan.FromMilliseconds(220))
            {
                EasingFunction = ease,
            };
            Storyboard.SetTarget(spin, ExpandChevron);
            Storyboard.SetTargetProperty(spin, new PropertyPath("(UIElement.RenderTransform).(RotateTransform.Angle)"));
            sb.Children.Add(spin);
        }

        if (CollapseChevron is not null && expanded)
        {
            CollapseChevron.RenderTransformOrigin = new Point(0.5, 0.5);
            CollapseChevron.RenderTransform = new RotateTransform(0);
            var spin = new DoubleAnimation(0, 180, TimeSpan.FromMilliseconds(220))
            {
                EasingFunction = ease,
            };
            Storyboard.SetTarget(spin, CollapseChevron);
            Storyboard.SetTargetProperty(spin, new PropertyPath("(UIElement.RenderTransform).(RotateTransform.Angle)"));
            sb.Children.Add(spin);
        }

        _activeStoryboard = sb;
        sb.Completed += (_, _) =>
        {
            Width = target;
            MinWidth = expanded ? MinExpandedWidth : CollapsedWidth;
            if (CollapsedRail is not null)
            {
                CollapsedRail.Opacity = expanded ? 0 : 1;
            }

            if (ExpandedPanel is not null)
            {
                ExpandedPanel.Opacity = expanded ? 1 : 0;
                if (ExpandedPanel.RenderTransform is TranslateTransform t)
                {
                    t.X = 0;
                }
            }

            if (ExpandChevron?.RenderTransform is RotateTransform er)
            {
                er.Angle = 0;
            }

            if (CollapseChevron?.RenderTransform is RotateTransform cr)
            {
                cr.Angle = 0;
            }

            _activeStoryboard = null;
        };
        sb.Begin();
    }

    private void ResizeGrip_OnMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (_vm?.IsExpanded != true)
        {
            return;
        }

        _activeStoryboard?.Stop();
        _activeStoryboard = null;
        _resizing = true;
        _resizeStart = e.GetPosition(null);
        _resizeStartWidth = ActualWidth > 0 ? ActualWidth : Width;
        ResizeGrip.CaptureMouse();
        e.Handled = true;
    }

    private void ResizeGrip_OnMouseMove(object sender, MouseEventArgs e)
    {
        if (!_resizing || e.LeftButton != MouseButtonState.Pressed)
        {
            return;
        }

        var pos = e.GetPosition(null);
        // Grip is on the left edge: drag left → wider panel, drag right → narrower.
        var delta = _resizeStart.X - pos.X;
        var next = ClampExpandedWidth(_resizeStartWidth + delta);
        Width = next;
        MinWidth = MinExpandedWidth;
        _userExpandedWidth = next;
        e.Handled = true;
    }

    private void ResizeGrip_OnMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (!_resizing)
        {
            return;
        }

        _resizing = false;
        if (ResizeGrip.IsMouseCaptured)
        {
            ResizeGrip.ReleaseMouseCapture();
        }

        _userExpandedWidth = ClampExpandedWidth(ActualWidth > 0 ? ActualWidth : Width);
        Width = _userExpandedWidth;
        e.Handled = true;
    }
}
