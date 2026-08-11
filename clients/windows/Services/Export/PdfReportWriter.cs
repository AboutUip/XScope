using QuestPDF.Fluent;
using QuestPDF.Helpers;
using QuestPDF.Infrastructure;

namespace XScope.Services.Export;

internal static class PdfReportWriter
{
    static PdfReportWriter()
    {
        QuestPDF.Settings.License = LicenseType.Community;
    }

    public static void Write(string path, MdReportDocument document)
    {
        var exportedAt = DateTime.Now.ToString("yyyy-MM-dd HH:mm");

        Document.Create(container =>
        {
            container.Page(page =>
            {
                page.Size(PageSizes.A4);
                page.Margin(48);
                page.DefaultTextStyle(x => x.FontSize(11).FontColor(Colors.Grey.Darken4));

                page.Header().Column(col =>
                {
                    col.Item().Row(row =>
                    {
                        row.RelativeItem().Text("XScope").SemiBold().FontSize(10).FontColor(Colors.Blue.Medium);
                        row.ConstantItem(180).AlignRight().Text(exportedAt).FontSize(9).FontColor(Colors.Grey.Medium);
                    });
                    col.Item().PaddingTop(4).Text(document.Title).SemiBold().FontSize(16);
                    col.Item().PaddingTop(8).LineHorizontal(1).LineColor(Colors.Grey.Lighten2);
                    col.Item().PaddingBottom(12);
                });

                page.Content().Column(col =>
                {
                    col.Spacing(10);
                    foreach (var block in document.Blocks)
                    {
                        RenderBlock(col, block);
                    }
                });

                page.Footer().AlignCenter().Text(text =>
                {
                    text.Span("Page ").FontSize(9).FontColor(Colors.Grey.Medium);
                    text.CurrentPageNumber().FontSize(9).FontColor(Colors.Grey.Medium);
                    text.Span(" / ").FontSize(9).FontColor(Colors.Grey.Medium);
                    text.TotalPages().FontSize(9).FontColor(Colors.Grey.Medium);
                });
            });
        }).GeneratePdf(path);
    }

    private static void RenderBlock(ColumnDescriptor col, MdBlock block)
    {
        switch (block)
        {
            case MdHeadingBlock heading:
                col.Item().Text(t =>
                {
                    t.DefaultTextStyle(x => x
                        .FontSize(heading.Level switch { 1 => 18, 2 => 15, 3 => 13, _ => 12 })
                        .SemiBold());
                    WriteInlines(t, heading.Inlines);
                });
                break;

            case MdParagraphBlock paragraph:
                col.Item().Text(t => WriteInlines(t, paragraph.Inlines));
                break;

            case MdQuoteBlock quote:
                col.Item().BorderLeft(3).BorderColor(Colors.Grey.Medium).PaddingLeft(10)
                    .Text(t =>
                    {
                        t.DefaultTextStyle(x => x.Italic().FontColor(Colors.Grey.Darken2));
                        WriteInlines(t, quote.Inlines);
                    });
                break;

            case MdCodeBlock code:
                col.Item().Background(Colors.Grey.Lighten4).Padding(8)
                    .Text(code.Code)
                    .FontFamily("Consolas")
                    .FontSize(9);
                break;

            case MdListBlock list:
                for (var i = 0; i < list.Items.Count; i++)
                {
                    var index = i;
                    col.Item().Row(row =>
                    {
                        row.ConstantItem(18).Text(list.Ordered ? $"{index + 1}." : "•");
                        row.RelativeItem().Text(t => WriteInlines(t, list.Items[index].Inlines));
                    });
                }

                break;

            case MdTableBlock table:
                if (table.Rows.Count == 0)
                {
                    break;
                }

                var cols = table.Rows.Max(r => r.Count);
                col.Item().Table(t =>
                {
                    t.ColumnsDefinition(def =>
                    {
                        for (var c = 0; c < cols; c++)
                        {
                            def.RelativeColumn();
                        }
                    });

                    for (var r = 0; r < table.Rows.Count; r++)
                    {
                        var isHeader = table.HasHeader && r == 0;
                        var row = table.Rows[r];
                        for (var c = 0; c < cols; c++)
                        {
                            var cell = c < row.Count ? row[c] : Array.Empty<MdInline>();
                            t.Cell().Border(0.5f).BorderColor(Colors.Grey.Lighten1)
                                .Background(isHeader ? Colors.Grey.Lighten3 : Colors.White)
                                .Padding(6)
                                .Text(text =>
                                {
                                    if (isHeader)
                                    {
                                        text.DefaultTextStyle(x => x.SemiBold().FontSize(10));
                                    }
                                    else
                                    {
                                        text.DefaultTextStyle(x => x.FontSize(10));
                                    }

                                    WriteInlines(text, cell);
                                });
                        }
                    }
                });
                break;

            case MdThematicBreakBlock:
                col.Item().PaddingVertical(4).LineHorizontal(1).LineColor(Colors.Grey.Lighten2);
                break;
        }
    }

    private static void WriteInlines(TextDescriptor text, IReadOnlyList<MdInline> inlines)
    {
        if (inlines.Count == 0)
        {
            text.Span("");
            return;
        }

        foreach (var inline in inlines)
        {
            var span = text.Span(inline.Text ?? "");
            if (inline.Bold)
            {
                span.SemiBold();
            }

            if (inline.Italic)
            {
                span.Italic();
            }

            if (inline.Code)
            {
                span.FontFamily("Consolas").BackgroundColor(Colors.Grey.Lighten3);
            }

            if (!string.IsNullOrWhiteSpace(inline.LinkUrl))
            {
                span.FontColor(Colors.Blue.Medium).Underline();
            }
        }
    }
}
