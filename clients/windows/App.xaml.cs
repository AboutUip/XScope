using System.Windows;
using System.Windows.Threading;
using XScope.Services;

namespace XScope;

public partial class App : Application
{
    public App()
    {
        DispatcherUnhandledException += OnDispatcherUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnUnhandledException;
        TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
    }

    private void OnStartup(object sender, StartupEventArgs e)
    {
        Loc.Instance.Initialize(); // default English unless ui.json says zh-Hans

        // Splash is temporary; Finish() promotes MainWindow and restores OnMainWindowClose.
        ShutdownMode = ShutdownMode.OnExplicitShutdown;
        var splash = new SplashWindow();
        MainWindow = splash;
        splash.Show();
    }

    private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        ShowError("UI error", e.Exception);
        e.Handled = true;
    }

    private static void OnUnhandledException(object? sender, UnhandledExceptionEventArgs e)
    {
        if (e.ExceptionObject is Exception ex)
        {
            ShowError("Fatal error", ex);
        }
    }

    private static void OnUnobservedTaskException(object? sender, UnobservedTaskExceptionEventArgs e)
    {
        ShowError("Background error", e.Exception);
        e.SetObserved();
    }

    private static void ShowError(string title, Exception ex)
    {
        try
        {
            MessageBox.Show(ex.Message, $"XScope — {title}", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        catch
        {
            // ignore secondary failures while reporting
        }
    }
}
