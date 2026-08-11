using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace XScope.Controls;

/// <summary>
/// Collapsible settings row. Header stays visible; click toggles body.
/// Avoids WPF Expander template/HeaderSite hit-test pitfalls.
/// </summary>
public class SettingsAccordionItem : HeaderedContentControl
{
    public static readonly DependencyProperty IsExpandedProperty =
        DependencyProperty.Register(
            nameof(IsExpanded),
            typeof(bool),
            typeof(SettingsAccordionItem),
            new FrameworkPropertyMetadata(
                false,
                FrameworkPropertyMetadataOptions.BindsTwoWayByDefault));

    public static readonly DependencyProperty ForceExpandedProperty =
        DependencyProperty.Register(
            nameof(ForceExpanded),
            typeof(bool),
            typeof(SettingsAccordionItem),
            new PropertyMetadata(false, OnForceExpandedChanged));

    public bool IsExpanded
    {
        get => (bool)GetValue(IsExpandedProperty);
        set => SetValue(IsExpandedProperty, value);
    }

    /// <summary>When true, keeps the section expanded (e.g. GitHub device-flow polling).</summary>
    public bool ForceExpanded
    {
        get => (bool)GetValue(ForceExpandedProperty);
        set => SetValue(ForceExpandedProperty, value);
    }

    public override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        if (GetTemplateChild("PART_Header") is UIElement header)
        {
            header.MouseLeftButtonDown -= OnHeaderMouseLeftButtonDown;
            header.MouseLeftButtonDown += OnHeaderMouseLeftButtonDown;
        }
    }

    private void OnHeaderMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (ForceExpanded)
        {
            IsExpanded = true;
            e.Handled = true;
            return;
        }

        IsExpanded = !IsExpanded;
        e.Handled = true;
    }

    private static void OnForceExpandedChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is SettingsAccordionItem item && e.NewValue is true)
        {
            item.IsExpanded = true;
        }
    }
}
