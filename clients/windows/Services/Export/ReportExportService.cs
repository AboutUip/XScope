using System.IO;
using System.Text;

namespace XScope.Services.Export;

internal static class ReportExportService
{
    public static void Export(
        string markdown,
        ReportExportFormat format,
        string path,
        string fallbackTitle)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException("Export path is empty.", nameof(path));
        }

        var dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(dir))
        {
            Directory.CreateDirectory(dir);
        }

        switch (format)
        {
            case ReportExportFormat.Markdown:
                File.WriteAllText(path, NormalizeMarkdown(markdown), new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
                break;

            case ReportExportFormat.Docx:
            {
                var doc = MarkdownBlockParser.Parse(markdown, fallbackTitle);
                DocxReportWriter.Write(path, doc);
                break;
            }

            case ReportExportFormat.Pdf:
            {
                var doc = MarkdownBlockParser.Parse(markdown, fallbackTitle);
                PdfReportWriter.Write(path, doc);
                break;
            }

            default:
                throw new ArgumentOutOfRangeException(nameof(format), format, null);
        }
    }

    public static string DefaultExtension(ReportExportFormat format) => format switch
    {
        ReportExportFormat.Markdown => ".md",
        ReportExportFormat.Pdf => ".pdf",
        ReportExportFormat.Docx => ".docx",
        _ => ".bin",
    };

    public static string FileFilter(ReportExportFormat format) => format switch
    {
        ReportExportFormat.Markdown => "Markdown (*.md)|*.md",
        ReportExportFormat.Pdf => "PDF (*.pdf)|*.pdf",
        ReportExportFormat.Docx => "Word document (*.docx)|*.docx",
        _ => "All files (*.*)|*.*",
    };

    public static string SanitizeFileName(string? name)
    {
        var raw = string.IsNullOrWhiteSpace(name) ? "research-report" : name.Trim();
        foreach (var c in Path.GetInvalidFileNameChars())
        {
            raw = raw.Replace(c, '_');
        }

        raw = raw.Trim('.', ' ');
        return string.IsNullOrWhiteSpace(raw) ? "research-report" : raw;
    }

    private static string NormalizeMarkdown(string markdown) =>
        (markdown ?? "").Replace("\r\n", "\n").Replace('\r', '\n').Trim() + "\n";
}
