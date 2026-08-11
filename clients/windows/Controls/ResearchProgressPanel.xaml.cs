using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using XScope.ViewModels;

namespace XScope.Controls;

public partial class ResearchProgressPanel : UserControl
{
    private ResearchProgressViewModel? _subscribed;
    private DispatcherTimer? _scrollDebounce;
    private bool _scrollPending;

    public ResearchProgressPanel()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
        Unloaded += (_, _) =>
        {
            Unsubscribe();
            _scrollDebounce?.Stop();
        };
    }

    private void OnDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        Unsubscribe();
        if (e.NewValue is ResearchProgressViewModel vm)
        {
            _subscribed = vm;
            vm.FeedUpdated += OnFeedUpdated;
        }
    }

    private void Unsubscribe()
    {
        if (_subscribed is not null)
        {
            _subscribed.FeedUpdated -= OnFeedUpdated;
            _subscribed = null;
        }
    }

    private void OnFeedUpdated()
    {
        if (_subscribed?.IsFeedExpanded != true || !IsVisible)
        {
            return;
        }

        _scrollPending = true;
        _scrollDebounce ??= new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(200),
        };
        _scrollDebounce.Tick -= ScrollDebounce_OnTick;
        _scrollDebounce.Tick += ScrollDebounce_OnTick;
        _scrollDebounce.Stop();
        _scrollDebounce.Start();
    }

    private void ScrollDebounce_OnTick(object? sender, EventArgs e)
    {
        _scrollDebounce?.Stop();
        if (!_scrollPending || FeedList is null || _subscribed?.IsFeedExpanded != true)
        {
            return;
        }

        _scrollPending = false;
        if (FeedList.Items.Count == 0)
        {
            return;
        }

        try
        {
            FeedList.ScrollIntoView(FeedList.Items[^1]);
        }
        catch
        {
        }
    }

    private void ExpandFeed_OnClick(object sender, RoutedEventArgs e)
    {
        if (DataContext is ResearchProgressViewModel vm)
        {
            vm.IsFeedExpanded = true;
        }
    }

    private void CollapseFeed_OnClick(object sender, RoutedEventArgs e)
    {
        if (DataContext is ResearchProgressViewModel vm)
        {
            vm.IsFeedExpanded = false;
        }
    }
}
