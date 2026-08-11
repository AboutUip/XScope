using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;
using XScope.ViewModels;

namespace XScope.Controls;

public partial class SearchComposePanel : UserControl
{
    private static readonly Color GoogleBlue = Color.FromRgb(0x1A, 0x73, 0xE8);
    private static readonly Color GoogleBlueSoft = Color.FromRgb(0xE8, 0xF0, 0xFE);
    private static readonly Color Ink = Color.FromRgb(0x20, 0x21, 0x24);
    private static readonly Color Muted = Color.FromRgb(0x5F, 0x63, 0x68);
    private static readonly Color Hairline = Color.FromRgb(0xE8, 0xEA, 0xED);
    private static readonly Color White = Colors.White;

    public static readonly DependencyProperty CompactProperty =
        DependencyProperty.Register(
            nameof(Compact),
            typeof(bool),
            typeof(SearchComposePanel),
            new PropertyMetadata(false, OnCompactChanged));

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
        };
    }

    public bool Compact
    {
        get => (bool)GetValue(CompactProperty);
        set => SetValue(CompactProperty, value);
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
        SearchChrome.CornerRadius = new CornerRadius(compact ? 24 : 26);
        SearchChrome.Effect = compact
            ? null
            : new DropShadowEffect
            {
                BlurRadius = 8,
                ShadowDepth = 1,
                Opacity = 0.12,
                Color = Color.FromRgb(0x20, 0x21, 0x24),
            };
        SyncSearchChromeHeight();
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

        var (chipBg, chipFg, chipChevron) = level switch
        {
            0 => (Color.FromRgb(0xF1, 0xF3, 0xF4), Color.FromRgb(0x3C, 0x40, 0x43), Color.FromRgb(0x80, 0x86, 0x8B)),
            1 => (GoogleBlueSoft, Ink, Muted),
            2 => (Color.FromRgb(0xD2, 0xE3, 0xFC), Color.FromRgb(0x17, 0x4E, 0xA6), Color.FromRgb(0x17, 0x4E, 0xA6)),
            _ => (GoogleBlueSoft, GoogleBlue, GoogleBlue),
        };

        PrecisionToggle.ApplyTemplate();
        if (PrecisionToggle.Template?.FindName("PrecisionChipBd", PrecisionToggle) is Border chipBd)
        {
            SetBrush(chipBd, Border.BackgroundProperty, chipBg);
        }

        SetBrush(PrecisionChipLabel, TextBlock.ForegroundProperty, chipFg);
        SetBrush(PrecisionChipChevron, Control.ForegroundProperty, chipChevron);

        // Popup stays white Google chrome — max energy lives only on the slider 流光.
        SetBrush(PrecisionPopupRoot, Border.BackgroundProperty, White);
        SetBrush(PrecisionPopupRoot, Border.BorderBrushProperty, Hairline);
        PrecisionPopupRoot.BorderThickness = new Thickness(1);
        SetBrush(PrecisionPopupTitle, TextBlock.ForegroundProperty, Muted);
        SetBrush(PrecisionPopupValue, TextBlock.ForegroundProperty, level >= 1 ? GoogleBlue : Ink);
        SetBrush(PrecisionHintLine, TextBlock.ForegroundProperty, Muted);
        SetBrush(PrecisionDetailLine, TextBlock.ForegroundProperty, Color.FromRgb(0x80, 0x86, 0x8B));

        PrecisionWash.Opacity = 0;
        PrecisionEdgeGlow.Opacity = 0;
        PrecisionAccentBar.Opacity = 0;

        if (PrecisionPopupShadow is not null)
        {
            PrecisionPopupShadow.Color = Color.FromRgb(0x20, 0x21, 0x24);
            PrecisionPopupShadow.Opacity = 0.14;
            PrecisionPopupShadow.BlurRadius = 12;
            PrecisionPopupShadow.ShadowDepth = 1;
        }

        var labels = new[] { LabelQuick, LabelNormal, LabelDeep, LabelMax };
        for (var i = 0; i < labels.Length; i++)
        {
            var active = i == level;
            SetBrush(labels[i], TextBlock.ForegroundProperty, active ? GoogleBlue : Muted);
            labels[i].FontWeight = active ? FontWeights.SemiBold : FontWeights.Normal;
            labels[i].Opacity = active ? 1.0 : 0.72;
        }

        SyncFlowActive();
    }

    private void SyncFlowActive() =>
        PrecisionFlow.FlowActive = PrecisionPopup.IsOpen && _appliedLevel == 3;

    private static void SetBrush(DependencyObject target, DependencyProperty dp, Color color) =>
        target.SetValue(dp, new SolidColorBrush(color));

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
