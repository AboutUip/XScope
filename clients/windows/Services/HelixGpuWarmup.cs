using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using System.Windows.Threading;
using HelixToolkit.Wpf;

namespace XScope.Services;

/// <summary>
/// Primes HelixToolkit / WPF-3D / D3D during splash so later knowledge-graph
/// views only update scene data instead of paying first-device creation cost.
/// </summary>
internal static class HelixGpuWarmup
{
    private static int _state; // 0 = idle, 1 = running, 2 = done
    private static HwndSource? _source;

    public static bool IsReady => Volatile.Read(ref _state) == 2;

    /// <summary>Kick off once; safe to call repeatedly. Prefer splash Loaded.</summary>
    public static void Begin(Dispatcher dispatcher)
    {
        if (Interlocked.CompareExchange(ref _state, 1, 0) != 0)
        {
            return;
        }

        // Run after the splash first paint so the intro stays smooth.
        dispatcher.BeginInvoke(static () =>
        {
            try
            {
                PrimeCore();
            }
            catch
            {
                // GPU warmup is best-effort — never block startup.
                Interlocked.Exchange(ref _state, 0);
                DisposeSource();
            }
        }, DispatcherPriority.ApplicationIdle);
    }

    private static void PrimeCore()
    {
        // Tiny off-screen HWND — no taskbar flash, no activation.
        var parameters = new HwndSourceParameters("xscope-helix-gpu-warmup")
        {
            Width = 16,
            Height = 16,
            PositionX = -32000,
            PositionY = -32000,
            WindowStyle = unchecked((int)0x80000000), // WS_POPUP
            UsesPerPixelOpacity = false,
        };

        _source = new HwndSource(parameters);
        var viewport = new HelixViewport3D
        {
            Width = 16,
            Height = 16,
            Background = Brushes.White,
            ShowCoordinateSystem = false,
            ShowViewCube = false,
            ShowCameraInfo = false,
            ShowFrameRate = false,
            IsHeadLightEnabled = true,
            ZoomExtentsWhenLoaded = true,
        };
        viewport.Children.Add(new DefaultLights());
        viewport.Children.Add(new SphereVisual3D
        {
            Center = new Point3D(0, 0, 0),
            Radius = 0.4,
            Fill = Brushes.SteelBlue,
        });
        viewport.Children.Add(new LinesVisual3D
        {
            Color = Colors.Gray,
            Thickness = 1,
            Points = { new Point3D(-1, 0, 0), new Point3D(1, 0, 0) },
        });

        _source.RootVisual = viewport;

        // Force measure/arrange/render so D3D device + Helix pipeline initialize now.
        viewport.Measure(new Size(16, 16));
        viewport.Arrange(new Rect(0, 0, 16, 16));
        viewport.UpdateLayout();
        try
        {
            viewport.ZoomExtents(1);
        }
        catch
        {
        }

        // One more render tick, then tear down the host (device stays warm in-process).
        viewport.Dispatcher.BeginInvoke(static () =>
        {
            try
            {
                // Touch RenderSize to ensure a frame was produced.
                _ = (_source?.RootVisual as FrameworkElement)?.RenderSize;
            }
            catch
            {
            }
            finally
            {
                DisposeSource();
                Interlocked.Exchange(ref _state, 2);
            }
        }, DispatcherPriority.Render);
    }

    private static void DisposeSource()
    {
        try
        {
            _source?.Dispose();
        }
        catch
        {
        }

        _source = null;
    }
}
