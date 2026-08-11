using System.Windows;
using System.Windows.Input;
using XScope.Services;
using XScope.ViewModels;

namespace XScope;

public partial class ExportReportWindow : Window
{
    private readonly ExportReportViewModel _vm;

    public ExportReportWindow(string markdown, string suggestedBaseName)
    {
        InitializeComponent();
        WindowThemeChrome.Attach(this);
        _vm = new ExportReportViewModel(markdown, suggestedBaseName);
        DataContext = _vm;
        _vm.CloseRequested += () => Dispatcher.BeginInvoke(Close);
        _vm.ExportSucceeded += OnExportSucceeded;
        Closed += (_, _) => _vm.ExportSucceeded -= OnExportSucceeded;
    }

    public string? ExportedPath { get; private set; }

    private void OnExportSucceeded(string path)
    {
        ExportedPath = path;
    }

    private void OnMarkdownCardClick(object sender, MouseButtonEventArgs e)
    {
        _vm.SelectedFormat = Services.Export.ReportExportFormat.Markdown;
    }

    private void OnPdfCardClick(object sender, MouseButtonEventArgs e)
    {
        _vm.SelectedFormat = Services.Export.ReportExportFormat.Pdf;
    }

    private void OnDocxCardClick(object sender, MouseButtonEventArgs e)
    {
        _vm.SelectedFormat = Services.Export.ReportExportFormat.Docx;
    }
}
