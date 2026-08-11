using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using MdXaml;
using XScope.Services;
using XScope.ViewModels;

namespace XScope;

public partial class MainWindow : Window
{
    private readonly MainShellViewModel _shell = new();
    private SettingsWindow? _settings;
    private bool _stickResearchScrollToBottom = true;
    private bool _researchScrollProgrammatic;

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _shell;
        WindowThemeChrome.Attach(this);
        ApplyReportMarkdownTheme();
        ThemeService.ThemeChanged += OnThemeChanged;
        _shell.OpenSettingsRequested += OpenSettings;
        _shell.ExportReportRequested += OpenExportReport;
        _shell.PromptRenameRequested += PromptRenameAsync;
        _shell.ConfirmDeleteRequested += ConfirmDeleteAsync;
        _shell.Research.FeedUpdated += OnResearchFeedUpdated;
        _shell.Research.HistoryRestored += OnResearchHistoryRestored;
        Closed += (_, _) =>
        {
            ThemeService.ThemeChanged -= OnThemeChanged;
            _shell.OpenSettingsRequested -= OpenSettings;
            _shell.ExportReportRequested -= OpenExportReport;
            _shell.PromptRenameRequested -= PromptRenameAsync;
            _shell.ConfirmDeleteRequested -= ConfirmDeleteAsync;
            _shell.Research.FeedUpdated -= OnResearchFeedUpdated;
            _shell.Research.HistoryRestored -= OnResearchHistoryRestored;
            _shell.Dispose();
            _settings?.Close();
        };
    }

    private void OnThemeChanged()
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(OnThemeChanged);
            return;
        }

        ApplyReportMarkdownTheme();
    }

    private void ApplyReportMarkdownTheme()
    {
        // Standard/GithubLike bake light zebra (#F5F5F5 / LightGray) into tables — unreadable on dark.
        // Sasabune derives zebra/code from Foreground alpha and stays readable in both themes.
        ReportMarkdownViewer.MarkdownStyle = ThemeService.IsDarkEffective
            ? MarkdownStyle.Sasabune
            : MarkdownStyle.SasabuneStandard;
    }

    /// <summary>
    /// MdXaml / FlowDocumentScrollViewer marks MouseWheel handled even with scrollbars disabled,
    /// so the outer research column never moves. Tunnel here and drive the outer viewer directly.
    /// </summary>
    private void ResearchWorkspaceScroll_OnPreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (e.Handled || sender is not ScrollViewer sv)
        {
            return;
        }

        ScrollResearchByDelta(sv, e.Delta);
        e.Handled = true;
    }

    /// <summary>
    /// Follow-up bar sits above the scroll surface; keep wheel scrolling the research column.
    /// </summary>
    private void FollowUpCompose_OnPreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (e.Handled || ResearchWorkspaceScroll is null)
        {
            return;
        }

        ScrollResearchByDelta(ResearchWorkspaceScroll, e.Delta);
        e.Handled = true;
    }

    private void ScrollResearchByDelta(ScrollViewer sv, int delta)
    {
        // User wheel → stop auto-stick so ScrollToEnd does not fight them.
        _stickResearchScrollToBottom = false;
        sv.ScrollToVerticalOffset(sv.VerticalOffset - delta);
    }

    private void ResearchWorkspaceScroll_OnScrollChanged(object sender, ScrollChangedEventArgs e)
    {
        if (sender is not ScrollViewer sv || _researchScrollProgrammatic)
        {
            return;
        }

        // User scrolled away from bottom → stop forcing follow; near bottom → resume.
        const double threshold = 48;
        var distance = sv.ScrollableHeight - sv.VerticalOffset;
        _stickResearchScrollToBottom = distance <= threshold;
    }

    private void OnResearchHistoryRestored()
    {
        // Always land on the latest content when opening a historical project.
        ScrollResearchToEnd(force: true, passCount: 3);
    }

    private void OnResearchFeedUpdated()
    {
        Dispatcher.BeginInvoke(() =>
        {
            // New run / near-empty feed: resume follow.
            if (_shell.Research.Feed.Count <= 2)
            {
                _stickResearchScrollToBottom = true;
            }

            if (!_stickResearchScrollToBottom)
            {
                return;
            }

            ScrollResearchToEnd(force: false, passCount: 1);
        }, System.Windows.Threading.DispatcherPriority.Background);
    }

    private void ScrollResearchToEnd(bool force, int passCount)
    {
        if (force)
        {
            _stickResearchScrollToBottom = true;
        }

        void Pass(int remaining)
        {
            if (ResearchWorkspaceScroll is null || (!_stickResearchScrollToBottom && !force))
            {
                return;
            }

            _researchScrollProgrammatic = true;
            try
            {
                ResearchWorkspaceScroll.UpdateLayout();
                ResearchWorkspaceScroll.ScrollToEnd();
            }
            finally
            {
                _researchScrollProgrammatic = false;
            }

            if (remaining > 1)
            {
                Dispatcher.BeginInvoke(() => Pass(remaining - 1),
                    System.Windows.Threading.DispatcherPriority.Loaded);
            }
        }

        Dispatcher.BeginInvoke(() => Pass(Math.Max(1, passCount)),
            System.Windows.Threading.DispatcherPriority.Loaded);
    }

    private void OpenSettings()
    {
        if (_settings is { IsLoaded: true })
        {
            if (_settings.WindowState == WindowState.Minimized)
            {
                _settings.WindowState = WindowState.Normal;
            }

            _settings.Activate();
            return;
        }

        _settings = new SettingsWindow
        {
            Owner = this,
            ShowInTaskbar = false,
        };
        _settings.Closed += (_, _) =>
        {
            _settings = null;
            _shell.RefreshAiOptions();
            // Owned settings can leave the shell behind other apps after close.
            Dispatcher.BeginInvoke(() =>
            {
                if (!IsVisible)
                {
                    return;
                }

                if (WindowState == WindowState.Minimized)
                {
                    WindowState = WindowState.Normal;
                }

                Activate();
                Topmost = true;
                Topmost = false;
                Focus();
            }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
        };
        _settings.Show();
        _settings.Activate();
    }

    private void OpenExportReport()
    {
        var markdown = _shell.Research.ReportMarkdown;
        if (string.IsNullOrWhiteSpace(markdown))
        {
            return;
        }

        var suggested = string.IsNullOrWhiteSpace(_shell.ActiveProjectTitle)
            ? Loc.Instance.ResearchReportTitle
            : _shell.ActiveProjectTitle;

        var dlg = new ExportReportWindow(markdown, suggested)
        {
            Owner = this,
        };
        dlg.ShowDialog();
        if (!string.IsNullOrWhiteSpace(dlg.ExportedPath))
        {
            _shell.StatusText = Loc.Instance.ExportSucceeded;
        }
    }

    private Task<string?> PromptRenameAsync(string currentTitle, string? prompt)
    {
        var dlg = new Window
        {
            Title = Loc.Instance.T("project.rename"),
            Owner = this,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            ResizeMode = ResizeMode.NoResize,
            Width = 420,
            SizeToContent = SizeToContent.Height,
            Background = System.Windows.Media.Brushes.White,
            FontFamily = FontFamily,
        };

        var box = new TextBox
        {
            Text = currentTitle,
            Margin = new Thickness(20, 12, 20, 0),
            FontSize = 14,
            Padding = new Thickness(10, 8, 10, 8),
        };
        var ok = new Button
        {
            Content = Loc.Instance.T("project.rename.ok"),
            MinWidth = 88,
            Margin = new Thickness(0, 0, 8, 0),
            IsDefault = true,
            Style = TryFindResource("MaterialDesignRaisedButton") as Style,
        };
        var cancel = new Button
        {
            Content = Loc.Instance.T("project.rename.cancel"),
            MinWidth = 88,
            IsCancel = true,
            Style = TryFindResource("MaterialDesignOutlinedButton") as Style,
        };

        string? result = null;
        ok.Click += (_, _) =>
        {
            result = box.Text;
            dlg.DialogResult = true;
        };
        cancel.Click += (_, _) => dlg.DialogResult = false;

        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(20, 20, 20, 16),
            Children = { ok, cancel },
        };
        var stack = new StackPanel();
        stack.Children.Add(new TextBlock
        {
            Text = prompt ?? Loc.Instance.T("project.rename.prompt"),
            Margin = new Thickness(20, 20, 20, 0),
            Foreground = new System.Windows.Media.SolidColorBrush(
                System.Windows.Media.Color.FromRgb(0x5F, 0x63, 0x68)),
            TextWrapping = TextWrapping.Wrap,
        });
        stack.Children.Add(box);
        stack.Children.Add(buttons);
        dlg.Content = stack;
        dlg.Loaded += (_, _) =>
        {
            box.Focus();
            box.SelectAll();
        };

        return Task.FromResult(dlg.ShowDialog() == true ? result?.Trim() : null);
    }

    private Task<bool> ConfirmDeleteAsync(string title)
    {
        var msg = string.Format(Loc.Instance.T("project.delete.confirm"), title);
        var r = MessageBox.Show(
            this,
            msg,
            Loc.Instance.T("project.delete"),
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning);
        return Task.FromResult(r == MessageBoxResult.Yes);
    }

    private void ProjectMore_OnClick(object sender, RoutedEventArgs e)
    {
        if (sender is not Button btn || btn.ContextMenu is null)
        {
            return;
        }

        var menu = btn.ContextMenu;
        menu.PlacementTarget = btn;
        menu.Placement = System.Windows.Controls.Primitives.PlacementMode.Bottom;
        menu.HorizontalOffset = -8;
        menu.VerticalOffset = 2;
        menu.DataContext = btn.DataContext;

        // Keep ⋮ visible while the menu is open (mouse leaves the row).
        btn.Opacity = 1;
        void OnClosed(object s, RoutedEventArgs args)
        {
            menu.Closed -= OnClosed;
            btn.ClearValue(OpacityProperty);
        }

        menu.Closed += OnClosed;
        menu.IsOpen = true;
        e.Handled = true;
    }
}
