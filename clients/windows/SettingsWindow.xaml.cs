using System.Windows;
using System.Windows.Controls;
using XScope.ViewModels;

namespace XScope;

public partial class SettingsWindow : Window
{
    private readonly SettingsViewModel _vm = new();

    public SettingsWindow()
    {
        InitializeComponent();
        DataContext = _vm;
        WindowThemeChrome.Attach(this);
        Closed += (_, _) => _vm.Dispose();
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            await _vm.InitializeAsync();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "XScope — Settings", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void OnPatPasswordChanged(object sender, RoutedEventArgs e)
    {
        if (sender is PasswordBox box)
        {
            _vm.GitHub.PatToken = box.Password;
        }
    }

    private void OnAiApiKeyChanged(object sender, RoutedEventArgs e)
    {
        if (sender is PasswordBox box && box.DataContext is AiProviderSettingsViewModel provider)
        {
            provider.ApiKey = box.Password;
        }
    }

    private void OnSearchApiKeyChanged(object sender, RoutedEventArgs e)
    {
        if (sender is PasswordBox box && box.DataContext is SearchModuleItemViewModel module)
        {
            module.ApiKey = box.Password;
        }
    }

    private void OnSettingsSearchTextChanged(object sender, TextChangedEventArgs e)
    {
        // MaterialDesign TextBox occasionally skips TwoWay source updates; keep VM in sync.
        if (sender is TextBox box && _vm.SettingsSearch != box.Text)
        {
            _vm.SettingsSearch = box.Text;
        }
    }
}
