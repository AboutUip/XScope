using System.Windows;
using System.Windows.Controls;
using XScope.Services;
using XScope.ViewModels;

namespace XScope;

public partial class MainWindow : Window
{
    private readonly MainShellViewModel _shell = new();
    private SettingsWindow? _settings;

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _shell;
        _shell.OpenSettingsRequested += OpenSettings;
        _shell.PromptRenameRequested += PromptRenameAsync;
        _shell.ConfirmDeleteRequested += ConfirmDeleteAsync;
        Closed += (_, _) =>
        {
            _shell.OpenSettingsRequested -= OpenSettings;
            _shell.PromptRenameRequested -= PromptRenameAsync;
            _shell.ConfirmDeleteRequested -= ConfirmDeleteAsync;
            _shell.Dispose();
            _settings?.Close();
        };
    }

    private void OpenSettings()
    {
        if (_settings is { IsLoaded: true })
        {
            _settings.Activate();
            return;
        }

        _settings = new SettingsWindow { Owner = this };
        _settings.Closed += (_, _) =>
        {
            _settings = null;
            _shell.RefreshAiOptions();
        };
        _settings.Show();
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
