using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Win32;
using XScope.Services;
using XScope.Services.Export;

namespace XScope.ViewModels;

internal sealed partial class ExportReportViewModel : ObservableObject
{
    private readonly string _markdown;
    private readonly string _suggestedBaseName;

    public ExportReportViewModel(string markdown, string suggestedBaseName)
    {
        _markdown = markdown ?? "";
        _suggestedBaseName = ReportExportService.SanitizeFileName(suggestedBaseName);
        L = Loc.Instance;
    }

    public Loc L { get; }

    public event Action? CloseRequested;
    public event Action<string>? ExportSucceeded;

    [ObservableProperty]
    private ReportExportFormat _selectedFormat = ReportExportFormat.Markdown;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError))]
    private string _errorText = "";

    [ObservableProperty]
    private bool _isBusy;

    public bool HasError => !string.IsNullOrWhiteSpace(ErrorText);

    public bool IsMarkdown
    {
        get => SelectedFormat == ReportExportFormat.Markdown;
        set
        {
            if (value)
            {
                SelectedFormat = ReportExportFormat.Markdown;
            }
        }
    }

    public bool IsPdf
    {
        get => SelectedFormat == ReportExportFormat.Pdf;
        set
        {
            if (value)
            {
                SelectedFormat = ReportExportFormat.Pdf;
            }
        }
    }

    public bool IsDocx
    {
        get => SelectedFormat == ReportExportFormat.Docx;
        set
        {
            if (value)
            {
                SelectedFormat = ReportExportFormat.Docx;
            }
        }
    }

    partial void OnSelectedFormatChanged(ReportExportFormat value)
    {
        OnPropertyChanged(nameof(IsMarkdown));
        OnPropertyChanged(nameof(IsPdf));
        OnPropertyChanged(nameof(IsDocx));
        ErrorText = "";
    }

    [RelayCommand]
    private void Cancel() => CloseRequested?.Invoke();

    [RelayCommand]
    private async Task ExportAsync()
    {
        if (IsBusy)
        {
            return;
        }

        ErrorText = "";
        var ext = ReportExportService.DefaultExtension(SelectedFormat);
        var dlg = new SaveFileDialog
        {
            Title = L.ExportReport,
            FileName = _suggestedBaseName + ext,
            Filter = ReportExportService.FileFilter(SelectedFormat),
            AddExtension = true,
            DefaultExt = ext.TrimStart('.'),
            OverwritePrompt = true,
        };

        if (dlg.ShowDialog() != true)
        {
            return;
        }

        var path = dlg.FileName;
        IsBusy = true;
        try
        {
            var markdown = _markdown;
            var format = SelectedFormat;
            var title = _suggestedBaseName;
            await Task.Run(() => ReportExportService.Export(markdown, format, path, title));
            ExportSucceeded?.Invoke(path);
            CloseRequested?.Invoke();
        }
        catch (Exception ex)
        {
            ErrorText = string.IsNullOrWhiteSpace(ex.Message)
                ? L.ExportFailed
                : ex.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }
}
