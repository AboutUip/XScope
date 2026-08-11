using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;
using XScope.Services;
using XScope.ViewModels;

namespace XScope.Controls;

public partial class SearchComposePanel : UserControl
{
    public static readonly DependencyProperty CompactProperty =
        DependencyProperty.Register(
            nameof(Compact),
            typeof(bool),
            typeof(SearchComposePanel),
            new PropertyMetadata(false, OnCompactChanged));

    public static readonly DependencyProperty PreferPopupAboveProperty =
        DependencyProperty.Register(
            nameof(PreferPopupAbove),
            typeof(bool),
            typeof(SearchComposePanel),
            new PropertyMetadata(false));

    private MainShellViewModel? _shell;
    private int _appliedLevel = -1;
    private bool _chromeReady;
    private bool _compact;

    public SearchComposePanel()
    {
        InitializeComponent();
        ApplyCompact(false);
        DataContextChanged += OnDataContextChanged;
        Loaded += (_, _) =>
        {
            _chromeReady = true;
            ApplyPrecisionChrome(force: true);
            SyncSearchChromeHeight();
            PrecisionPopup.CustomPopupPlacementCallback = PlacePrecisionPopup;
            ThemeService.ThemeChanged += OnThemeChanged;
        };
        Unloaded += (_, _) => ThemeService.ThemeChanged -= OnThemeChanged;
    }

    private void OnThemeChanged()
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(OnThemeChanged);
            return;
        }

        ApplyPrecisionChrome(force: true);
        ApplySearchChromeShadow();
    }

    public bool Compact
    {
        get => (bool)GetValue(CompactProperty);
        set => SetValue(CompactProperty, value);
    }

    /// <summary>When true (follow-up bar at window bottom), open precision popup above the toggle.</summary>
    public bool PreferPopupAbove
    {
        get => (bool)GetValue(PreferPopupAboveProperty);
        set => SetValue(PreferPopupAboveProperty, value);
    }

    private static void OnCompactChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is SearchComposePanel panel)
        {
            panel.ApplyCompact(e.NewValue is true);
        }
    }

    private void ApplyCompact(bool compact)
    {
        _compact = compact;
        SearchChrome.MinHeight = compact ? 48 : 52;
        SearchChrome.MaxHeight = compact ? 140 : 160;
        var radius = compact ? 24 : 26;
        SearchChrome.CornerRadius = new CornerRadius(radius);
        if (SearchChromeFill is not null)
        {
            SearchChromeFill.CornerRadius = new CornerRadius(Math.Max(0, radius - 1));
        }

        ApplySearchChromeShadow();
        SyncSearchChromeHeight();
    }

    private void ApplySearchChromeShadow()
    {
        // DropShadow on a stroked/rounded border paints a black fringe at curves in dark theme.
        if (_compact || ThemeService.IsDarkEffective)
        {
            SearchChrome.Effect = null;
            return;
        }

        SearchChrome.Effect = new DropShadowEffect
        {
            BlurRadius = 8,
            ShadowDepth = 1,
            Opacity = 0.12,
            Color = Color.FromRgb(0x20, 0x21, 0x24),
        };
    }

    private void OnSearchPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key is not (Key.Return or Key.Enter))
        {
            return;
        }

        // Shift+Enter → newline (AcceptsReturn). Enter alone → submit.
        if ((Keyboard.Modifiers & ModifierKeys.Shift) == ModifierKeys.Shift)
        {
            return;
        }

        e.Handled = true;
        if (DataContext is MainShellViewModel vm && vm.SubmitSearchCommand.CanExecute(null))
        {
            vm.SubmitSearchCommand.Execute(null);
        }
    }

    private void OnSearchTextChanged(object sender, TextChangedEventArgs e) => SyncSearchChromeHeight();

    private void SyncSearchChromeHeight()
    {
        if (SearchBox is null || SearchChrome is null)
        {
            return;
        }

        var lineCount = Math.Max(1, SearchBox.LineCount);
        var lineHeight = SearchBox.FontSize * 1.35;
        var pad = 20.0;
        var desired = pad + lineCount * lineHeight;
        var min = _compact ? 48.0 : 52.0;
        var max = SearchChrome.MaxHeight;
        SearchChrome.Height = Math.Clamp(desired, min, max);
        SearchBox.VerticalContentAlignment = lineCount > 1 ? VerticalAlignment.Top : VerticalAlignment.Center;
    }

    private void OnDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (_shell is not null)
        {
            _shell.PropertyChanged -= OnShellPropertyChanged;
        }

        _shell = e.NewValue as MainShellViewModel;
        if (_shell is not null)
        {
            _shell.PropertyChanged += OnShellPropertyChanged;
        }

        ApplyPrecisionChrome(force: true);
    }

    private void OnShellPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(MainShellViewModel.PrecisionIndex)
            or nameof(MainShellViewModel.ActivePrecisionKind)
            or nameof(MainShellViewModel.SelectedPrecision)
            or nameof(MainShellViewModel.ActivePrecisionLabel)
            or nameof(MainShellViewModel.L))
        {
            ApplyPrecisionChrome();
            if (PrecisionPopup.IsOpen)
            {
                Dispatcher.BeginInvoke(AlignPrecisionLabels);
            }
        }
    }

    private void OnPrecisionFlowValueChanged(object sender, RoutedPropertyChangedEventArgs<int> e) =>
        ApplyPrecisionChrome();

    private void OnPrecisionFlowSizeChanged(object sender, SizeChangedEventArgs e) =>
        AlignPrecisionLabels();

    private void ApplyPrecisionChrome(bool force = false)
    {
        if (!_chromeReady && !IsLoaded)
        {
            return;
        }

        var level = _shell?.PrecisionIndex ?? PrecisionFlow.Value;
        level = Math.Clamp(level, 0, 3);
        if (!force && level == _appliedLevel)
        {
            SyncFlowActive();
            return;
        }

        _appliedLevel = level;

        var accent = ThemeColor("XScopeAccent", Color.FromRgb(0x1A, 0x73, 0xE8));
        var accentText = ThemeColor("XScopeAccentText", Color.FromRgb(0x19, 0x67, 0xD2));
        var ink = ThemeColor("XScopeTextPrimary", Color.FromRgb(0x20, 0x21, 0x24));
        var muted = ThemeColor("XScopeTextSecondary", Color.FromRgb(0x5F, 0x63, 0x68));
        var faint = ThemeColor("XScopeTextMuted", Color.FromRgb(0x9A, 0xA0, 0xA6));
        var surface = ThemeColor("XScopeSurfaceAlt", Colors.White);
        var border = ThemeColor("XScopeBorder", Color.FromRgb(0xE8, 0xEA, 0xED));
        var dark = ThemeService.IsDarkEffective;

        var (chipBg, chipFg, chipChevron) = level switch
        {
            0 => (Colors.Transparent, ink, faint),
            1 => (Colors.Transparent, ink, muted),
            2 => (Colors.Transparent, accentText, accentText),
            _ => (Colors.Transparent, accent, accent),
        };

        PrecisionToggle.ApplyTemplate();
        if (PrecisionToggle.Template?.FindName("PrecisionChipBd", PrecisionToggle) is Border chipBd)
        {
            SetBrush(chipBd, Border.BackgroundProperty, chipBg);
        }

        SetBrush(PrecisionChipLabel, TextBlock.ForegroundProperty, chipFg);
        SetBrush(PrecisionChipChevron, Control.ForegroundProperty, chipChevron);

        SetBrush(PrecisionPopupRoot, Border.BackgroundProperty, surface);
        SetBrush(PrecisionPopupRoot, Border.BorderBrushProperty, border);
        PrecisionPopupRoot.BorderThickness = new Thickness(1);
        SetBrush(PrecisionPopupTitle, TextBlock.ForegroundProperty, muted);
        SetBrush(PrecisionPopupValue, TextBlock.ForegroundProperty, level >= 1 ? accent : ink);
        SetBrush(PrecisionHintLine, TextBlock.ForegroundProperty, muted);
        SetBrush(PrecisionDetailLine, TextBlock.ForegroundProperty, faint);

        PrecisionWash.Opacity = 0;
        PrecisionEdgeGlow.Opacity = 0;
        PrecisionAccentBar.Opacity = 0;

        if (PrecisionPopupShadow is not null)
        {
            PrecisionPopupShadow.Color = dark ? Colors.Black : Color.FromRgb(0x20, 0x21, 0x24);
            PrecisionPopupShadow.Opacity = dark ? 0.45 : 0.14;
            PrecisionPopupShadow.BlurRadius = dark ? 18 : 12;
            PrecisionPopupShadow.ShadowDepth = 1;
        }

        var labels = new[] { LabelQuick, LabelNormal, LabelDeep, LabelMax };
        for (var i = 0; i < labels.Length; i++)
        {
            var active = i == level;
            SetBrush(labels[i], TextBlock.ForegroundProperty, active ? accent : muted);
            labels[i].FontWeight = active ? FontWeights.SemiBold : FontWeights.Normal;
            labels[i].Opacity = active ? 1.0 : 0.72;
        }

        PrecisionFlow.ApplyThemeColors(accent, border, dark);
        SyncFlowActive();
    }

    private static Color ThemeColor(string key, Color fallback)
    {
        if (Application.Current?.TryFindResource(key) is SolidColorBrush brush)
        {
            return brush.Color;
        }

        return fallback;
    }

    private static void SetBrush(DependencyObject target, DependencyProperty dp, Color color) =>
        target.SetValue(dp, new SolidColorBrush(color));

    private void SyncFlowActive() =>
        PrecisionFlow.FlowActive = PrecisionPopup.IsOpen && _appliedLevel == 3;

    private void OnProviderMenuOpen(object sender, RoutedEventArgs e) => ProviderPopup.IsOpen = true;

    private void OnProviderMenuClose(object sender, RoutedEventArgs e)
    {
        if (ProviderPopup.IsOpen)
        {
            ProviderPopup.IsOpen = false;
        }
    }

    private void OnProviderPopupClosed(object? sender, EventArgs e) =>
        ProviderToggle.IsChecked = false;

    private void OnProviderPicked(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: SearchProviderOption option } &&
            DataContext is MainShellViewModel vm)
        {
            vm.SelectedProvider = option;
        }

        ProviderPopup.IsOpen = false;
    }

    private void OnModelMenuOpen(object sender, RoutedEventArgs e) => ModelPopup.IsOpen = true;

    private void OnModelMenuClose(object sender, RoutedEventArgs e)
    {
        if (ModelPopup.IsOpen)
        {
            ModelPopup.IsOpen = false;
        }
    }

    private void OnModelPopupClosed(object? sender, EventArgs e) => ModelToggle.IsChecked = false;

    private void OnModelPicked(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: SearchModelOption option } &&
            DataContext is MainShellViewModel vm)
        {
            vm.SelectedModel = option;
        }

        ModelPopup.IsOpen = false;
    }

    private void OnPrecisionMenuOpen(object sender, RoutedEventArgs e) => PrecisionPopup.IsOpen = true;

    private void OnPrecisionMenuClose(object sender, RoutedEventArgs e)
    {
        if (PrecisionPopup.IsOpen)
        {
            PrecisionPopup.IsOpen = false;
        }
    }

    private void OnPrecisionPopupClosed(object? sender, EventArgs e)
    {
        PrecisionToggle.IsChecked = false;
        SyncFlowActive();
    }

    private void OnPrecisionPopupOpened(object? sender, EventArgs e)
    {
        ApplyPrecisionChrome(force: true);
        Dispatcher.BeginInvoke(() =>
        {
            AlignPrecisionLabels();
            SyncFlowActive();
        }, System.Windows.Threading.DispatcherPriority.Loaded);
    }

    private CustomPopupPlacement[] PlacePrecisionPopup(Size popupSize, Size targetSize, Point offset)
    {
        // Center popup under/over the toggle; keep horizontal shift from overflowing left.
        var x = Math.Min(0, targetSize.Width - popupSize.Width);
        if (PreferPopupAbove || ShouldOpenPrecisionAbove(popupSize.Height))
        {
            return
            [
                new CustomPopupPlacement(new Point(x, -popupSize.Height - 6), PopupPrimaryAxis.Horizontal),
                new CustomPopupPlacement(new Point(x, targetSize.Height + 6), PopupPrimaryAxis.Horizontal),
            ];
        }

        return
        [
            new CustomPopupPlacement(new Point(x, targetSize.Height + 6), PopupPrimaryAxis.Horizontal),
            new CustomPopupPlacement(new Point(x, -popupSize.Height - 6), PopupPrimaryAxis.Horizontal),
        ];
    }

    private bool ShouldOpenPrecisionAbove(double popupHeight)
    {
        try
        {
            var toggle = PrecisionToggle;
            var window = Window.GetWindow(toggle);
            if (window is null)
            {
                return PreferPopupAbove;
            }

            var screen = toggle.PointToScreen(new Point(0, toggle.ActualHeight));
            var source = PresentationSource.FromVisual(window);
            if (source?.CompositionTarget is not null)
            {
                screen = source.CompositionTarget.TransformFromDevice.Transform(screen);
            }

            var spaceBelow = window.Top + window.ActualHeight - screen.Y;
            return spaceBelow < popupHeight + 24;
        }
        catch
        {
            return PreferPopupAbove;
        }
    }

    private void AlignPrecisionLabels()
    {
        if (PrecisionLabelCanvas.ActualWidth < 8)
        {
            return;
        }

        var rail = Math.Max(1, PrecisionLabelCanvas.ActualWidth);
        var labels = new[] { LabelQuick, LabelNormal, LabelDeep, LabelMax };
        for (var i = 0; i < labels.Length; i++)
        {
            var label = labels[i];
            label.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            var w = label.DesiredSize.Width;
            var center = rail * (i / 3.0);
            var x = Math.Clamp(center - w / 2, 0, Math.Max(0, rail - w));
            Canvas.SetLeft(label, x);
            Canvas.SetTop(label, 0);
        }
    }
}
