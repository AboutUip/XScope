using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using System.Windows.Threading;
using HelixToolkit.Wpf;
using XScope.Services;

namespace XScope.Controls;

public partial class KnowledgeGraph3DView : UserControl
{
    public static readonly DependencyProperty GraphJsonProperty =
        DependencyProperty.Register(
            nameof(GraphJson),
            typeof(string),
            typeof(KnowledgeGraph3DView),
            new PropertyMetadata(null, OnGraphJsonChanged));

    private readonly List<SimNode> _nodes = [];
    private readonly List<SimEdge> _edges = [];
    private readonly Dictionary<string, int> _indexById = new(StringComparer.Ordinal);
    private readonly List<OverlayLabel> _overlayLabels = [];
    private string _appliedJson = "";
    private bool _showAllLabels;
    private HelixViewport3D? _viewport;
    private ModelVisual3D? _graphRoot;
    private LinesVisual3D? _lines;
    private LinesVisual3D? _highlightLines;
    private int _applySerial;
    private int _selectedIndex = -1;
    private Point _pressPos;
    private bool _pressMoved;
    private bool _labelsHooked;
    private bool _cameraInteracting;
    private DispatcherTimer? _labelRefreshTimer;
    private int _labelRefreshSerial;
    private DispatcherTimer? _resizeSettleTimer;
    private int _resizeSerial;
    private bool _sizeFrozen;

    public KnowledgeGraph3DView()
    {
        InitializeComponent();
        IsVisibleChanged += OnIsVisibleChanged;
        SizeChanged += OnViewSizeChanged;
        ThemeService.ThemeChanged += OnThemeChanged;
        Unloaded += (_, _) =>
        {
            ThemeService.ThemeChanged -= OnThemeChanged;
            _labelRefreshTimer?.Stop();
            _resizeSettleTimer?.Stop();
            UnhookCamera();
            TearDownViewport();
        };
        Loaded += (_, _) =>
        {
            ApplyThemeSurface();
            TryApplyPending();
        };
    }

    private void OnThemeChanged()
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.BeginInvoke(OnThemeChanged);
            return;
        }

        ApplyThemeSurface();
    }

    private void ApplyThemeSurface()
    {
        var bg = Application.Current?.TryFindResource("XScopeWindowBg") as Brush
                 ?? (ThemeService.IsDarkEffective
                     ? new SolidColorBrush(Color.FromRgb(0x0F, 0x14, 0x19))
                     : Brushes.White);
        Background = bg;
        if (_viewport is not null)
        {
            _viewport.Background = bg;
        }

        ApplyEdgeVisualTheme();
        ScheduleOverlayLabelRefresh(force: true);
        if (_selectedIndex >= 0)
        {
            RefreshSelectionChrome(_selectedIndex);
        }
    }

    private void ApplyEdgeVisualTheme()
    {
        var dark = ThemeService.IsDarkEffective;
        if (_lines is not null)
        {
            _lines.Color = dark
                ? Color.FromRgb(0x3A, 0x3F, 0x44)
                : Color.FromRgb(0xC5, 0xCA, 0xD1);
            _lines.Thickness = 0.7;
        }

        if (_highlightLines is not null)
        {
            _highlightLines.Color = dark
                ? Color.FromRgb(0x1D, 0x9B, 0xF0)
                : Color.FromRgb(0x1A, 0x73, 0xE8);
            _highlightLines.Thickness = 1.45;
        }
    }

    public string? GraphJson
    {
        get => (string?)GetValue(GraphJsonProperty);
        set => SetValue(GraphJsonProperty, value);
    }

    private static void OnGraphJsonChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is KnowledgeGraph3DView view)
        {
            view.TryApplyPending();
        }
    }

    private void OnIsVisibleChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (IsVisible)
        {
            TryApplyPending();
            ScheduleOverlayLabelRefresh(force: true);
        }
    }

    private void TryApplyPending()
    {
        if (!IsLoaded)
        {
            return;
        }

        var json = GraphJson ?? "";
        if (string.Equals(json, _appliedJson, StringComparison.Ordinal))
        {
            if (!HasRenderableNodes(json))
            {
                ShowEmpty();
            }

            return;
        }

        var serial = ++_applySerial;
        Dispatcher.BeginInvoke(() =>
        {
            if (serial != _applySerial || !IsLoaded)
            {
                return;
            }

            RebuildScene(GraphJson ?? "");
        }, DispatcherPriority.ApplicationIdle);
    }

    private static bool HasRenderableNodes(string? json)
    {
        if (string.IsNullOrWhiteSpace(json) || json is "{}" or "null")
        {
            return false;
        }

        try
        {
            using var doc = System.Text.Json.JsonDocument.Parse(json);
            var root = doc.RootElement;
            if (root.TryGetProperty("graph", out var nested) &&
                nested.ValueKind == System.Text.Json.JsonValueKind.Object)
            {
                root = nested;
            }

            return root.TryGetProperty("nodes", out var nodes) &&
                   nodes.ValueKind == System.Text.Json.JsonValueKind.Array &&
                   nodes.GetArrayLength() > 0;
        }
        catch
        {
            return false;
        }
    }

    private void ShowEmpty()
    {
        EmptyHint.Visibility = Visibility.Visible;
        Toolbar.Visibility = Visibility.Collapsed;
        InteractHint.Visibility = Visibility.Collapsed;
        DetailCard.Visibility = Visibility.Collapsed;
        ClearOverlayLabels();
        TearDownViewport();
        _appliedJson = GraphJson ?? "";
        _selectedIndex = -1;
    }

    private void TearDownViewport()
    {
        UnhookCamera();
        if (_viewport is not null)
        {
            _viewport.PreviewMouseLeftButtonDown -= Viewport_OnPreviewMouseLeftButtonDown;
            _viewport.PreviewMouseMove -= Viewport_OnPreviewMouseMove;
            _viewport.PreviewMouseLeftButtonUp -= Viewport_OnPreviewMouseLeftButtonUp;
            _viewport.PreviewMouseRightButtonDown -= Viewport_OnPreviewMouseRightButtonDown;
            _viewport.PreviewMouseRightButtonUp -= Viewport_OnPreviewMouseRightButtonUp;
            _viewport.PreviewMouseDown -= Viewport_OnPreviewMouseDown;
            _viewport.PreviewMouseUp -= Viewport_OnPreviewMouseUp;
            _viewport.PreviewMouseWheel -= Viewport_OnPreviewMouseWheel;
            _viewport.MouseDoubleClick -= Viewport_OnMouseDoubleClick;
        }

        _nodes.Clear();
        _edges.Clear();
        _indexById.Clear();
        _lines = null;
        _highlightLines = null;
        _graphRoot = null;
        _selectedIndex = -1;
        _sizeFrozen = false;
        ViewportHost.Children.Clear();
        _viewport = null;
    }

    private void HookCamera()
    {
        if (_viewport is null || _labelsHooked)
        {
            return;
        }

        _viewport.CameraChanged += Viewport_OnCameraChanged;
        _labelsHooked = true;
    }

    private void UnhookCamera()
    {
        if (_viewport is not null && _labelsHooked)
        {
            _viewport.CameraChanged -= Viewport_OnCameraChanged;
        }

        _labelsHooked = false;
    }

    private void Viewport_OnCameraChanged(object? sender, RoutedEventArgs e)
    {
        // Any active drag (incl. Helix middle-button pan) must hide overlays immediately —
        // otherwise labels stay at old screen coords and smear until a debounce tick.
        if (_sizeFrozen || IsPointerCameraDrag())
        {
            _cameraInteracting = true;
            LabelLayer.Opacity = 0;
            _labelRefreshTimer?.Stop();
            return;
        }

        ScheduleOverlayLabelRefresh(force: false);
    }

    private static bool IsPointerCameraDrag() =>
        Mouse.LeftButton == MouseButtonState.Pressed ||
        Mouse.RightButton == MouseButtonState.Pressed ||
        Mouse.MiddleButton == MouseButtonState.Pressed;

    private void OnViewSizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (_viewport is null || !IsVisible || !IsLoaded)
        {
            return;
        }

        // Helix re-renders the whole D3D surface on every layout pass — freeze size while
        // the user stretches the window, then snap once after resize settles.
        if (Math.Abs(e.NewSize.Width - e.PreviousSize.Width) < 0.5 &&
            Math.Abs(e.NewSize.Height - e.PreviousSize.Height) < 0.5)
        {
            return;
        }

        BeginSizeFreeze();
        _resizeSettleTimer ??= new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(100),
        };
        _resizeSettleTimer.Stop();
        var serial = ++_resizeSerial;
        _resizeSettleTimer.Tick -= OnResizeSettled;
        void OnResizeSettled(object? s, EventArgs args)
        {
            _resizeSettleTimer.Stop();
            _resizeSettleTimer.Tick -= OnResizeSettled;
            if (serial != _resizeSerial)
            {
                return;
            }

            EndSizeFreeze();
        }

        _resizeSettleTimer.Tick += OnResizeSettled;
        _resizeSettleTimer.Start();
    }

    private void BeginSizeFreeze()
    {
        LabelLayer.Opacity = 0;
        if (_viewport is null)
        {
            return;
        }

        if (_sizeFrozen)
        {
            return;
        }

        var w = _viewport.ActualWidth;
        var h = _viewport.ActualHeight;
        if (w < 2 || h < 2)
        {
            return;
        }

        _sizeFrozen = true;
        _viewport.Width = w;
        _viewport.Height = h;
        _viewport.HorizontalAlignment = HorizontalAlignment.Left;
        _viewport.VerticalAlignment = VerticalAlignment.Top;
    }

    private void EndSizeFreeze()
    {
        if (_viewport is not null && _sizeFrozen)
        {
            _viewport.Width = double.NaN;
            _viewport.Height = double.NaN;
            _viewport.HorizontalAlignment = HorizontalAlignment.Stretch;
            _viewport.VerticalAlignment = VerticalAlignment.Stretch;
        }

        _sizeFrozen = false;
        if (_cameraInteracting)
        {
            return;
        }

        ScheduleOverlayLabelRefresh(force: true);
    }

    private void ScheduleOverlayLabelRefresh(bool force)
    {
        if (_viewport is null || !IsVisible || _sizeFrozen)
        {
            return;
        }

        if (force)
        {
            LabelLayer.Opacity = 1;
            RefreshOverlayLabels();
            return;
        }

        _labelRefreshTimer ??= new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(40),
        };
        _labelRefreshTimer.Stop();
        var serial = ++_labelRefreshSerial;
        _labelRefreshTimer.Tick -= OnLabelRefreshTick;
        void OnLabelRefreshTick(object? s, EventArgs e)
        {
            _labelRefreshTimer.Stop();
            _labelRefreshTimer.Tick -= OnLabelRefreshTick;
            if (serial != _labelRefreshSerial || _cameraInteracting || _sizeFrozen)
            {
                return;
            }

            LabelLayer.Opacity = 1;
            RefreshOverlayLabels();
        }

        _labelRefreshTimer.Tick += OnLabelRefreshTick;
        _labelRefreshTimer.Start();
    }

    private void EnsureViewport()
    {
        if (_viewport is not null)
        {
            return;
        }

        _graphRoot = new ModelVisual3D();
        _viewport = new HelixViewport3D
        {
            Background = ThemeService.IsDarkEffective
                ? new SolidColorBrush(Color.FromRgb(0x00, 0x00, 0x00))
                : Brushes.White,
            ShowCoordinateSystem = false,
            ShowViewCube = true,
            ViewCubeHorizontalPosition = HorizontalAlignment.Right,
            ViewCubeVerticalPosition = VerticalAlignment.Top,
            ViewCubeOpacity = 0.55,
            ShowCameraInfo = false,
            ShowFrameRate = false,
            ZoomExtentsWhenLoaded = false,
            IsHeadLightEnabled = true,
            CameraRotationMode = CameraRotationMode.Turntable,
            RotateAroundMouseDownPoint = true,
            ZoomAroundMouseDownPoint = true,
            IsRotationEnabled = true,
            IsZoomEnabled = true,
            IsPanEnabled = true,
            ZoomSensitivity = 1.2,
            RotationSensitivity = 1.15,
            RotateGesture = new MouseGesture(MouseAction.LeftClick),
            PanGesture = new MouseGesture(MouseAction.RightClick),
            PanGesture2 = new MouseGesture(MouseAction.MiddleClick),
        };
        _viewport.Children.Add(new DefaultLights());
        _viewport.Children.Add(_graphRoot);
        _viewport.PreviewMouseLeftButtonDown += Viewport_OnPreviewMouseLeftButtonDown;
        _viewport.PreviewMouseMove += Viewport_OnPreviewMouseMove;
        _viewport.PreviewMouseLeftButtonUp += Viewport_OnPreviewMouseLeftButtonUp;
        _viewport.PreviewMouseRightButtonDown += Viewport_OnPreviewMouseRightButtonDown;
        _viewport.PreviewMouseRightButtonUp += Viewport_OnPreviewMouseRightButtonUp;
        _viewport.PreviewMouseDown += Viewport_OnPreviewMouseDown;
        _viewport.PreviewMouseUp += Viewport_OnPreviewMouseUp;
        _viewport.PreviewMouseWheel += Viewport_OnPreviewMouseWheel;
        _viewport.MouseDoubleClick += Viewport_OnMouseDoubleClick;
        ViewportHost.Children.Add(_viewport);
        HookCamera();
    }

    private void RebuildScene(string? json)
    {
        _appliedJson = json ?? "";
        _selectedIndex = -1;
        DetailCard.Visibility = Visibility.Collapsed;
        ClearOverlayLabels();

        if (!HasRenderableNodes(json))
        {
            ShowEmpty();
            return;
        }

        _nodes.Clear();
        _edges.Clear();
        _indexById.Clear();
        _lines = null;
        _highlightLines = null;

        try
        {
            using var doc = System.Text.Json.JsonDocument.Parse(json!);
            var root = doc.RootElement;
            if (root.TryGetProperty("graph", out var nested) &&
                nested.ValueKind == System.Text.Json.JsonValueKind.Object)
            {
                root = nested;
            }

            var rng = new Random(17);
            if (root.TryGetProperty("nodes", out var nodesEl) &&
                nodesEl.ValueKind == System.Text.Json.JsonValueKind.Array)
            {
                foreach (var n in nodesEl.EnumerateArray())
                {
                    if (_nodes.Count >= 120)
                    {
                        break;
                    }

                    var id = ReadString(n, "id");
                    if (string.IsNullOrWhiteSpace(id) || _indexById.ContainsKey(id))
                    {
                        continue;
                    }

                    var title = ReadFlexibleString(n, "title");
                    var kind = ReadFlexibleString(n, "kind") ?? "fact";
                    var content = ReadFlexibleString(n, "content") ?? "";
                    var summary = ReadFlexibleString(n, "summary") ?? "";
                    var weight = ReadWeight(n);
                    if (string.IsNullOrWhiteSpace(title))
                    {
                        title = id;
                    }
                    var depth = n.TryGetProperty("depth_layer", out var d) && d.TryGetInt32(out var di)
                        ? Math.Max(0, di)
                        : 0;
                    var pos = new Point3D(
                        (rng.NextDouble() - 0.5) * 8,
                        (rng.NextDouble() - 0.5) * 6,
                        depth * 2.2 + (rng.NextDouble() - 0.5) * 2);

                    _indexById[id] = _nodes.Count;
                    _nodes.Add(new SimNode
                    {
                        Id = id,
                        Title = CleanText(title),
                        Kind = kind,
                        Content = CleanText(content),
                        Summary = CleanText(summary),
                        Weight = weight,
                        Position = pos,
                    });
                }
            }

            if (root.TryGetProperty("edges", out var edgesEl) &&
                edgesEl.ValueKind == System.Text.Json.JsonValueKind.Array)
            {
                foreach (var e in edgesEl.EnumerateArray())
                {
                    var from = ReadString(e, "from_id") ?? ReadString(e, "from");
                    var to = ReadString(e, "to_id") ?? ReadString(e, "to");
                    var rel = ReadString(e, "relation") ?? ReadString(e, "type") ?? "related";
                    if (from is null || to is null ||
                        !_indexById.TryGetValue(from, out var fi) ||
                        !_indexById.TryGetValue(to, out var ti) ||
                        fi == ti)
                    {
                        continue;
                    }

                    _edges.Add(new SimEdge { From = fi, To = ti, Relation = rel });
                }
            }
        }
        catch
        {
            ShowEmpty();
            return;
        }

        if (_nodes.Count == 0)
        {
            ShowEmpty();
            return;
        }

        EnsureViewport();
        if (_graphRoot is null || _viewport is null)
        {
            ShowEmpty();
            return;
        }

        SimulateLayout(iterations: 90);
        EmptyHint.Visibility = Visibility.Collapsed;
        Toolbar.Visibility = Visibility.Visible;
        InteractHint.Visibility = Visibility.Visible;
        BuildVisuals();
        Dispatcher.BeginInvoke(() =>
        {
            try
            {
                _viewport?.ZoomExtents(180);
            }
            catch
            {
            }

            RefreshOverlayLabels();
        }, DispatcherPriority.Loaded);
    }

    private void SimulateLayout(int iterations)
    {
        const double repulsion = 18.0;
        const double spring = 0.12;
        const double damping = 0.82;
        const double rest = 2.6;
        const double centerPull = 0.02;
        var n = _nodes.Count;
        if (n == 0)
        {
            return;
        }

        var forceBuf = new Vector3D[n];
        for (var tick = 0; tick < iterations; tick++)
        {
            for (var i = 0; i < n; i++)
            {
                var force = new Vector3D();
                var a = _nodes[i].Position;
                for (var j = 0; j < n; j++)
                {
                    if (i == j)
                    {
                        continue;
                    }

                    var delta = a - _nodes[j].Position;
                    var dist = Math.Max(0.45, delta.Length);
                    force += delta / dist * (repulsion / (dist * dist));
                }

                force += new Vector3D(-a.X, -a.Y, -a.Z) * centerPull;
                forceBuf[i] = force;
            }

            foreach (var edge in _edges)
            {
                var delta = _nodes[edge.To].Position - _nodes[edge.From].Position;
                var dist = Math.Max(0.25, delta.Length);
                var springF = delta / dist * ((dist - rest) * spring);
                forceBuf[edge.From] += springF;
                forceBuf[edge.To] -= springF;
            }

            for (var i = 0; i < n; i++)
            {
                var step = forceBuf[i] * 0.055;
                if (step.Length > 1.8)
                {
                    step *= 1.8 / step.Length;
                }

                step *= Math.Pow(damping, 0.15);
                _nodes[i].Position += step;
            }
        }
    }

    private void BuildVisuals()
    {
        if (_graphRoot is null)
        {
            return;
        }

        _graphRoot.Children.Clear();

        // No 3D billboards — they fight depth with spheres. Labels are 2D overlays.
        var linePoints = new Point3DCollection(_edges.Count * 2);
        foreach (var edge in _edges)
        {
            linePoints.Add(_nodes[edge.From].Position);
            linePoints.Add(_nodes[edge.To].Position);
        }

        var dark = ThemeService.IsDarkEffective;
        _lines = new LinesVisual3D
        {
            Color = dark
                ? Color.FromRgb(0x3A, 0x3F, 0x44)
                : Color.FromRgb(0xC5, 0xCA, 0xD1),
            Thickness = 0.7,
            Points = linePoints,
        };
        _graphRoot.Children.Add(_lines);

        _highlightLines = new LinesVisual3D
        {
            Color = dark
                ? Color.FromRgb(0x1D, 0x9B, 0xF0)
                : Color.FromRgb(0x1A, 0x73, 0xE8),
            Thickness = 1.45,
            Points = new Point3DCollection(),
        };
        _graphRoot.Children.Add(_highlightLines);

        for (var i = 0; i < _nodes.Count; i++)
        {
            var node = _nodes[i];
            var sphere = new SphereVisual3D
            {
                Center = node.Position,
                Radius = RadiusForWeight(node.Weight),
                Fill = BrushForWeight(node.Weight),
            };
            _graphRoot.Children.Add(sphere);
            node.Sphere = sphere;
        }
    }

    private void ApplyPositionsToVisuals()
    {
        foreach (var node in _nodes)
        {
            if (node.Sphere is not null)
            {
                node.Sphere.Center = node.Position;
            }
        }

        if (_lines is not null)
        {
            var pts = new Point3DCollection(_edges.Count * 2);
            foreach (var edge in _edges)
            {
                pts.Add(_nodes[edge.From].Position);
                pts.Add(_nodes[edge.To].Position);
            }

            _lines.Points = pts;
        }

        if (_selectedIndex >= 0)
        {
            UpdateHighlightEdges(_selectedIndex);
        }

        RefreshOverlayLabels();
    }

    private void ClearOverlayLabels()
    {
        LabelLayer.Children.Clear();
        _overlayLabels.Clear();
    }

    private void RefreshOverlayLabels()
    {
        if (_viewport is null || !IsVisible || _nodes.Count == 0)
        {
            ClearOverlayLabels();
            return;
        }

        var viewport = _viewport.Viewport;
        var layerW = LabelLayer.ActualWidth;
        var layerH = LabelLayer.ActualHeight;
        if (layerW < 8 || layerH < 8)
        {
            return;
        }

        // Avoid covering the detail card.
        var bottomReserve = DetailCard.Visibility == Visibility.Visible
            ? Math.Min(DetailCard.ActualHeight + 16, layerH * 0.45)
            : 28;

        var neighbor = new HashSet<int>();
        if (_selectedIndex >= 0)
        {
            foreach (var e in _edges)
            {
                if (e.From == _selectedIndex)
                {
                    neighbor.Add(e.To);
                }
                else if (e.To == _selectedIndex)
                {
                    neighbor.Add(e.From);
                }
            }
        }

        // Default: every node with a title is a candidate (this is the missing-title fix).
        // Compact mode (_showAllLabels == false means "declutter"): still all nodes, but
        // collision may drop lower-rank ones. Toggle ON = keep more via smaller chips / more nudges.
        var degree = new int[_nodes.Count];
        foreach (var e in _edges)
        {
            degree[e.From]++;
            degree[e.To]++;
        }

        var candidates = new List<(int Index, int Rank)>();
        for (var i = 0; i < _nodes.Count; i++)
        {
            if (string.IsNullOrWhiteSpace(DisplayTitle(_nodes[i])))
            {
                continue;
            }

            int rank;
            if (i == _selectedIndex)
            {
                rank = 0;
            }
            else if (neighbor.Contains(i))
            {
                rank = 1;
            }
            else
            {
                // Higher degree → keep when decluttering.
                rank = 2 + Math.Max(0, 8 - degree[i]);
            }

            candidates.Add((i, rank));
        }

        candidates.Sort((a, b) =>
        {
            var c = a.Rank.CompareTo(b.Rank);
            if (c != 0)
            {
                return c;
            }

            c = degree[b.Index].CompareTo(degree[a.Index]);
            return c != 0 ? c : string.CompareOrdinal(_nodes[a.Index].Title, _nodes[b.Index].Title);
        });

        ClearOverlayLabels();
        var placed = new List<Rect>();
        var maxLabels = _showAllLabels ? _nodes.Count : Math.Min(_nodes.Count, 48);
        var shown = 0;

        foreach (var (index, rank) in candidates)
        {
            if (shown >= maxLabels && index != _selectedIndex)
            {
                break;
            }

            var node = _nodes[index];
            Point screenInViewport;
            try
            {
                screenInViewport = Viewport3DHelper.Point3DtoPoint2D(viewport, node.Position);
            }
            catch
            {
                continue;
            }

            if (double.IsNaN(screenInViewport.X) || double.IsNaN(screenInViewport.Y))
            {
                continue;
            }

            // Map Helix viewport coords → LabelLayer (sibling overlay) coords.
            Point screen;
            try
            {
                screen = _viewport.TranslatePoint(screenInViewport, LabelLayer);
            }
            catch
            {
                screen = screenInViewport;
            }

            if (screen.X < -60 || screen.Y < -40 ||
                screen.X > layerW + 60 || screen.Y > layerH - bottomReserve + 20)
            {
                continue;
            }

            var isSelected = index == _selectedIndex;
            var title = DisplayTitle(node);
            // Selected: keep more of the title; wrapping handles length (no ugly mega-pill).
            var text = isSelected
                ? Truncate(title, 48)
                : Truncate(title, _showAllLabels ? 16 : 14);
            var label = BuildOverlayChip(text, rank, isSelected);
            label.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            var size = label.DesiredSize;

            var x = screen.X - size.Width / 2;
            var y = screen.Y - size.Height - (isSelected ? 20 : 14);

            var placedOk = false;
            for (var attempt = 0; attempt < (isSelected ? 6 : (_showAllLabels ? 4 : 3)); attempt++)
            {
                var tryY = y - attempt * (size.Height * 0.85 + 2);
                var rect = new Rect(x, tryY, size.Width, size.Height);
                var hits = placed.Any(p => Inflate(p, isSelected ? 2 : 4).IntersectsWith(rect));
                if (!hits || isSelected)
                {
                    y = tryY;
                    placedOk = true;
                    break;
                }
            }

            if (!placedOk)
            {
                continue;
            }

            x = Math.Clamp(x, 2, Math.Max(2, layerW - size.Width - 2));
            y = Math.Clamp(y, 2, Math.Max(2, layerH - bottomReserve - size.Height - 2));

            Canvas.SetLeft(label, x);
            Canvas.SetTop(label, y);
            Panel.SetZIndex(label, isSelected ? 100 : 10 - Math.Min(rank, 9));
            LabelLayer.Children.Add(label);
            placed.Add(new Rect(x, y, size.Width, size.Height));
            _overlayLabels.Add(new OverlayLabel { Index = index, Element = label });
            shown++;
        }
    }

    private static string DisplayTitle(SimNode node)
    {
        if (!string.IsNullOrWhiteSpace(node.Title))
        {
            return node.Title.Trim();
        }

        return string.IsNullOrWhiteSpace(node.Id) ? "" : node.Id.Trim();
    }

    private static Rect Inflate(Rect r, double pad) =>
        new(r.X - pad, r.Y - pad, r.Width + pad * 2, r.Height + pad * 2);

    private Border BuildOverlayChip(string text, int rank, bool selected)
    {
        var dark = ThemeService.IsDarkEffective;
        var accent = ThemeRgb("XScopeAccent", Color.FromRgb(0x1A, 0x73, 0xE8));
        var fg = selected
            ? Colors.White
            : ThemeRgb("XScopeTextPrimary", Color.FromRgb(0x20, 0x21, 0x24));
        var bg = selected
            ? accent
            : rank <= 1
                ? (dark
                    ? Color.FromArgb(0xE8, 0x00, 0x2A, 0x43)
                    : Color.FromArgb(0xF5, 0xE8, 0xF0, 0xFE))
                : (dark
                    ? Color.FromArgb(0xE6, 0x1E, 0x20, 0x24)
                    : Color.FromArgb(0xF0, 0xFF, 0xFF, 0xFF));
        var border = selected
            ? accent
            : ThemeRgb("XScopeBorder", Color.FromRgb(0xE0, 0xE3, 0xE7));

        // Selected: capsule chip. Long titles wrap inside a max width so we don't get
        // a stretched "sausage/ellipse" from CornerRadius=∞ on a single long line.
        var block = new TextBlock
        {
            Text = text,
            FontSize = selected ? 12 : 10.5,
            FontWeight = selected ? FontWeights.SemiBold : FontWeights.Medium,
            Foreground = new SolidColorBrush(fg),
            TextWrapping = selected ? TextWrapping.Wrap : TextWrapping.NoWrap,
            TextAlignment = TextAlignment.Center,
            LineHeight = selected ? 16 : double.NaN,
        };

        if (selected)
        {
            block.MaxWidth = 168;
        }

        return new Border
        {
            Background = new SolidColorBrush(bg),
            BorderBrush = new SolidColorBrush(border),
            BorderThickness = new Thickness(selected ? 0 : 1),
            // ~capsule on 1–2 lines; fixed radius avoids ugly long stadium shapes.
            CornerRadius = new CornerRadius(selected ? 14 : 6),
            Padding = new Thickness(selected ? 10 : 6, selected ? 5 : 2, selected ? 10 : 6, selected ? 5 : 2),
            MaxWidth = selected ? 188 : double.PositiveInfinity,
            Child = block,
        };
    }

    private static Color ThemeRgb(string key, Color fallback)
    {
        if (Application.Current?.TryFindResource(key) is SolidColorBrush brush)
        {
            return brush.Color;
        }

        return fallback;
    }

    private static Brush ThemeBrush(string key, Color fallback) =>
        new SolidColorBrush(ThemeRgb(key, fallback));

    private void Viewport_OnPreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        _pressPos = e.GetPosition(_viewport);
        _pressMoved = false;
        BeginCameraInteraction();
    }

    private void Viewport_OnPreviewMouseMove(object sender, MouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed || _viewport is null)
        {
            return;
        }

        var p = e.GetPosition(_viewport);
        if (Math.Abs(p.X - _pressPos.X) + Math.Abs(p.Y - _pressPos.Y) > 5)
        {
            _pressMoved = true;
            BeginCameraInteraction();
        }
    }

    private void Viewport_OnPreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        var wasDragging = _pressMoved;
        _cameraInteracting = false;

        if (wasDragging || _viewport is null)
        {
            EndCameraInteraction();
            return;
        }

        var hits = _viewport.Viewport.FindHits(e.GetPosition(_viewport));
        foreach (var hit in hits)
        {
            if (hit.Visual is not SphereVisual3D sphere)
            {
                continue;
            }

            for (var i = 0; i < _nodes.Count; i++)
            {
                if (!ReferenceEquals(_nodes[i].Sphere, sphere))
                {
                    continue;
                }

                SelectNode(i);
                ScheduleOverlayLabelRefresh(force: true);
                return;
            }
        }

        ClearSelection();
        ScheduleOverlayLabelRefresh(force: true);
    }

    private void Viewport_OnPreviewMouseRightButtonDown(object sender, MouseButtonEventArgs e)
    {
        BeginCameraInteraction();
    }

    private void Viewport_OnPreviewMouseRightButtonUp(object sender, MouseButtonEventArgs e)
    {
        EndCameraInteraction();
    }

    private void Viewport_OnPreviewMouseDown(object sender, MouseButtonEventArgs e)
    {
        // Helix PanGesture2 = MiddleClick — must hide labels immediately (same as right-pan).
        if (e.ChangedButton == MouseButton.Middle)
        {
            BeginCameraInteraction();
        }
    }

    private void Viewport_OnPreviewMouseUp(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton == MouseButton.Middle)
        {
            EndCameraInteraction();
        }
    }

    private void BeginCameraInteraction()
    {
        _cameraInteracting = true;
        LabelLayer.Opacity = 0;
        _labelRefreshTimer?.Stop();
    }

    private void EndCameraInteraction()
    {
        _cameraInteracting = IsPointerCameraDrag();
        if (_cameraInteracting)
        {
            return;
        }

        ScheduleOverlayLabelRefresh(force: true);
    }

    private void Viewport_OnPreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        LabelLayer.Opacity = 0;
        _labelRefreshTimer?.Stop();
        ScheduleOverlayLabelRefresh(force: false);
    }

    private void Viewport_OnMouseDoubleClick(object? sender, MouseButtonEventArgs e)
    {
        if (_viewport is null || _selectedIndex < 0 || _selectedIndex >= _nodes.Count)
        {
            return;
        }

        try
        {
            _viewport.LookAt(_nodes[_selectedIndex].Position, 6.5, 280);
        }
        catch
        {
        }

        e.Handled = true;
    }

    private void SelectNode(int index)
    {
        if (index < 0 || index >= _nodes.Count)
        {
            return;
        }

        if (_selectedIndex >= 0 && _selectedIndex < _nodes.Count &&
            _nodes[_selectedIndex].Sphere is not null)
        {
            RestoreNodeVisual(_nodes[_selectedIndex]);
        }

        _selectedIndex = index;
        var node = _nodes[index];
        if (node.Sphere is not null)
        {
            node.Sphere.Radius = RadiusForWeight(node.Weight) * 1.35;
            node.Sphere.Fill = new SolidColorBrush(Color.FromRgb(0xD9, 0x30, 0x25));
        }

        DetailTitle.Text = string.IsNullOrWhiteSpace(node.Title) ? node.Id : node.Title;
        DetailKind.Text = FormatKind(node.Kind);
        DetailWeight.Text = $"权重 {node.Weight:0.00}";
        DetailId.Text = node.Id;

        RefreshSelectionChrome(index);

        var relations = _edges
            .Where(ed => ed.From == index || ed.To == index)
            .Select(ed =>
            {
                var outbound = ed.From == index;
                var peer = outbound ? ed.To : ed.From;
                return new RelationRow
                {
                    RelationLabel = FormatRelation(ed.Relation, outbound),
                    PeerTitle = string.IsNullOrWhiteSpace(_nodes[peer].Title)
                        ? _nodes[peer].Id
                        : _nodes[peer].Title,
                };
            })
            .OrderBy(r => r.RelationLabel)
            .ThenBy(r => r.PeerTitle)
            .ToList();

        RelationsList.ItemsSource = relations;
        RelationsHeader.Visibility = Visibility.Visible;
        RelationsEmpty.Visibility = relations.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        RelationsList.Visibility = relations.Count == 0 ? Visibility.Collapsed : Visibility.Visible;

        InteractHint.Visibility = Visibility.Collapsed;
        DetailCard.Visibility = Visibility.Visible;
        UpdateHighlightEdges(index);
        ScheduleOverlayLabelRefresh(force: true);
    }

    private void RefreshSelectionChrome(int index)
    {
        if (index < 0 || index >= _nodes.Count)
        {
            return;
        }

        var node = _nodes[index];
        var hasSummary = !string.IsNullOrWhiteSpace(node.Summary);
        var hasBody = !string.IsNullOrWhiteSpace(node.Content);
        AboutHeader.Visibility = Visibility.Visible;
        DetailSummary.Text = hasSummary
            ? node.Summary
            : (hasBody ? Truncate(node.Content, 280) : "暂无 AI 总结");
        DetailSummary.Foreground = hasSummary || hasBody
            ? ThemeBrush("XScopeTextPrimary", Color.FromRgb(0x20, 0x21, 0x24))
            : ThemeBrush("XScopeTextMuted", Color.FromRgb(0x9A, 0xA0, 0xA6));
        DetailSummary.FontStyle = hasSummary || hasBody ? FontStyles.Normal : FontStyles.Italic;

        BodyHeader.Visibility = hasBody ? Visibility.Visible : Visibility.Collapsed;
        DetailBody.Visibility = hasBody ? Visibility.Visible : Visibility.Collapsed;
        DetailBody.Text = hasBody ? node.Content : "";
        DetailBody.Foreground = ThemeBrush("XScopeTextPrimary", Color.FromRgb(0x20, 0x21, 0x24));
        DetailBody.FontStyle = FontStyles.Normal;
    }

    private void UpdateHighlightEdges(int index)
    {
        if (_highlightLines is null)
        {
            return;
        }

        var pts = new Point3DCollection();
        foreach (var edge in _edges)
        {
            if (edge.From != index && edge.To != index)
            {
                continue;
            }

            pts.Add(_nodes[edge.From].Position);
            pts.Add(_nodes[edge.To].Position);
        }

        _highlightLines.Points = pts;
    }

    private void ClearSelection()
    {
        if (_selectedIndex >= 0 && _selectedIndex < _nodes.Count &&
            _nodes[_selectedIndex].Sphere is not null)
        {
            RestoreNodeVisual(_nodes[_selectedIndex]);
        }

        _selectedIndex = -1;
        DetailCard.Visibility = Visibility.Collapsed;
        if (_highlightLines is not null)
        {
            _highlightLines.Points = new Point3DCollection();
        }

        if (_nodes.Count > 0)
        {
            InteractHint.Visibility = Visibility.Visible;
        }

        ScheduleOverlayLabelRefresh(force: true);
    }

    private void ClearSelectionButton_OnClick(object sender, RoutedEventArgs e) => ClearSelection();

    private void FitButton_OnClick(object sender, RoutedEventArgs e)
    {
        try
        {
            _viewport?.ZoomExtents(200);
            ScheduleOverlayLabelRefresh(force: true);
        }
        catch
        {
        }
    }

    private void LabelsButton_OnClick(object sender, RoutedEventArgs e)
    {
        // true = denser (keep more labels); false = declutter cap 48 with stricter collision.
        _showAllLabels = !_showAllLabels;
        LabelsButton.Opacity = _showAllLabels ? 1.0 : 0.7;
        ScheduleOverlayLabelRefresh(force: true);
    }

    private void RelayoutButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_nodes.Count == 0)
        {
            return;
        }

        var rng = new Random(Environment.TickCount);
        foreach (var node in _nodes)
        {
            node.Position = new Point3D(
                (rng.NextDouble() - 0.5) * 8,
                (rng.NextDouble() - 0.5) * 6,
                (rng.NextDouble() - 0.5) * 4);
        }

        SimulateLayout(iterations: 90);
        ApplyPositionsToVisuals();
        try
        {
            _viewport?.ZoomExtents(180);
        }
        catch
        {
        }

        ScheduleOverlayLabelRefresh(force: true);
    }

    private static string FormatKind(string kind) =>
        kind.Trim().ToLowerInvariant() switch
        {
            "entity" => "实体",
            "fact" => "事实",
            "finding" => "发现",
            "insight" => "洞察",
            "code" => "代码",
            "note" => "笔记",
            "event" => "事件",
            "claim" => "主张",
            _ => string.IsNullOrWhiteSpace(kind) ? "节点" : kind,
        };

    private static string FormatRelation(string rel, bool outbound)
    {
        var baseLabel = rel.Trim().ToLowerInvariant() switch
        {
            "part_of" => "组成",
            "owns" => "拥有",
            "depends_on" => "依赖",
            "cites" => "引用",
            "supports" => "支持",
            "contradicts" => "矛盾",
            "related" => "相关",
            _ => string.IsNullOrWhiteSpace(rel) ? "相关" : rel,
        };
        return outbound ? baseLabel : $"被{baseLabel}";
    }

    private static string CleanText(string s)
    {
        if (string.IsNullOrWhiteSpace(s))
        {
            return "";
        }

        var t = s.Replace("\r\n", "\n").Replace('\r', '\n').Trim();
        while (t.Contains("  ", StringComparison.Ordinal))
        {
            t = t.Replace("  ", " ", StringComparison.Ordinal);
        }

        while (t.Contains("\n\n\n", StringComparison.Ordinal))
        {
            t = t.Replace("\n\n\n", "\n\n", StringComparison.Ordinal);
        }

        return t;
    }

    private static SolidColorBrush Freeze(Color c)
    {
        var b = new SolidColorBrush(c);
        b.Freeze();
        return b;
    }

    private static void RestoreNodeVisual(SimNode node)
    {
        if (node.Sphere is null)
        {
            return;
        }

        node.Sphere.Radius = RadiusForWeight(node.Weight);
        node.Sphere.Fill = BrushForWeight(node.Weight);
    }

    private static double Clamp01(double v) =>
        v < 0 ? 0 : v > 1 ? 1 : v;

    private static double RadiusForWeight(double weight)
    {
        var w = Clamp01(weight);
        // Low-importance nodes stay small; core findings dominate the view.
        return 0.14 + w * 0.34;
    }

    private static SolidColorBrush BrushForWeight(double weight)
    {
        var w = Clamp01(weight);
        // Cool muted → Google blue → warm amber/red as importance rises.
        Color a;
        Color b;
        double t;
        if (w < 0.5)
        {
            a = Color.FromRgb(0x90, 0xA4, 0xAE);
            b = Color.FromRgb(0x1A, 0x73, 0xE8);
            t = w / 0.5;
        }
        else
        {
            a = Color.FromRgb(0x1A, 0x73, 0xE8);
            b = Color.FromRgb(0xD9, 0x30, 0x25);
            t = (w - 0.5) / 0.5;
        }

        var c = Color.FromRgb(
            (byte)(a.R + (b.R - a.R) * t),
            (byte)(a.G + (b.G - a.G) * t),
            (byte)(a.B + (b.B - a.B) * t));
        return Freeze(c);
    }

    private static string Truncate(string s, int max) =>
        s.Length <= max ? s : s[..(max - 1)] + "…";

    private static double ReadWeight(System.Text.Json.JsonElement el)
    {
        if (!el.TryGetProperty("weight", out var p))
        {
            return 0.5;
        }

        return p.ValueKind switch
        {
            System.Text.Json.JsonValueKind.Number when p.TryGetDouble(out var d) => Clamp01(d),
            System.Text.Json.JsonValueKind.String when double.TryParse(
                p.GetString(),
                System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture,
                out var ds) => Clamp01(ds),
            _ => 0.5,
        };
    }

    private static string? ReadString(System.Text.Json.JsonElement el, string name) =>
        ReadFlexibleString(el, name);

    private static string? ReadFlexibleString(System.Text.Json.JsonElement el, string name)
    {
        if (!el.TryGetProperty(name, out var p))
        {
            return null;
        }

        return p.ValueKind switch
        {
            System.Text.Json.JsonValueKind.String => p.GetString(),
            System.Text.Json.JsonValueKind.Number => p.ToString(),
            System.Text.Json.JsonValueKind.True => "true",
            System.Text.Json.JsonValueKind.False => "false",
            System.Text.Json.JsonValueKind.Null => null,
            _ => p.ToString(),
        };
    }

    private sealed class SimNode
    {
        public required string Id { get; init; }
        public required string Title { get; init; }
        public required string Kind { get; init; }
        public string Content { get; init; } = "";
        public string Summary { get; init; } = "";
        public double Weight { get; init; } = 0.5;
        public Point3D Position { get; set; }
        public SphereVisual3D? Sphere { get; set; }
    }

    private sealed class SimEdge
    {
        public required int From { get; init; }
        public required int To { get; init; }
        public required string Relation { get; init; }
    }

    private sealed class OverlayLabel
    {
        public int Index { get; init; }
        public UIElement Element { get; init; } = null!;
    }

    private sealed class RelationRow
    {
        public required string RelationLabel { get; init; }
        public required string PeerTitle { get; init; }
    }
}
