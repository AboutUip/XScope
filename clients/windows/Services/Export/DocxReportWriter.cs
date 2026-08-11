using DocumentFormat.OpenXml;
using DocumentFormat.OpenXml.Packaging;
using DocumentFormat.OpenXml.Wordprocessing;
using Dw = DocumentFormat.OpenXml.Wordprocessing;

namespace XScope.Services.Export;

internal static class DocxReportWriter
{
    public static void Write(string path, MdReportDocument document)
    {
        using var word = WordprocessingDocument.Create(path, WordprocessingDocumentType.Document);
        var main = word.AddMainDocumentPart();
        main.Document = new Document(new Body());
        var body = main.Document.Body!;

        foreach (var block in document.Blocks)
        {
            switch (block)
            {
                case MdHeadingBlock heading:
                    body.Append(CreateParagraph(heading.Inlines, outlineLevel: heading.Level, bold: true,
                        fontSizeHalfPoints: heading.Level switch
                        {
                            1 => 36,
                            2 => 28,
                            3 => 24,
                            _ => 22,
                        },
                        spaceAfter: 120));
                    break;

                case MdParagraphBlock paragraph:
                    body.Append(CreateParagraph(paragraph.Inlines, spaceAfter: 160));
                    break;

                case MdQuoteBlock quote:
                    body.Append(CreateParagraph(quote.Inlines, italic: true, indentLeft: 360, spaceAfter: 160));
                    break;

                case MdCodeBlock code:
                    body.Append(CreateCodeParagraph(code.Code));
                    break;

                case MdListBlock list:
                    for (var i = 0; i < list.Items.Count; i++)
                    {
                        var prefix = list.Ordered ? $"{i + 1}. " : "• ";
                        var runs = new List<MdInline> { new() { Text = prefix } };
                        runs.AddRange(list.Items[i].Inlines);
                        body.Append(CreateParagraph(runs, indentLeft: 360, spaceAfter: 80));
                    }

                    break;

                case MdTableBlock table:
                    body.Append(CreateTable(table));
                    body.Append(new Paragraph(new ParagraphProperties(
                        new SpacingBetweenLines { After = "160" })));
                    break;

                case MdThematicBreakBlock:
                    body.Append(new Paragraph(new ParagraphProperties(
                        new ParagraphBorders(
                            new BottomBorder
                            {
                                Val = BorderValues.Single,
                                Size = 6,
                                Color = "C5CAD1",
                                Space = 1,
                            }),
                        new SpacingBetweenLines { Before = "120", After = "120" })));
                    break;
            }
        }

        main.Document.Save();
    }

    private static Paragraph CreateParagraph(
        IReadOnlyList<MdInline> inlines,
        int? outlineLevel = null,
        bool bold = false,
        bool italic = false,
        int fontSizeHalfPoints = 22,
        int indentLeft = 0,
        int spaceAfter = 0)
    {
        var props = new ParagraphProperties();
        if (outlineLevel is int level)
        {
            props.Append(new OutlineLevel { Val = level - 1 });
        }

        if (indentLeft > 0 || spaceAfter > 0)
        {
            props.Append(new SpacingBetweenLines { After = spaceAfter.ToString() });
            if (indentLeft > 0)
            {
                props.Append(new Indentation { Left = indentLeft.ToString() });
            }
        }
        else if (spaceAfter > 0)
        {
            props.Append(new SpacingBetweenLines { After = spaceAfter.ToString() });
        }

        var paragraph = new Paragraph(props);
        foreach (var inline in inlines)
        {
            paragraph.Append(CreateRun(inline, forceBold: bold, forceItalic: italic, fontSizeHalfPoints));
        }

        if (inlines.Count == 0)
        {
            paragraph.Append(new Run(new Text("")));
        }

        return paragraph;
    }

    private static Paragraph CreateCodeParagraph(string code)
    {
        var paragraph = new Paragraph(new ParagraphProperties(
            new SpacingBetweenLines { After = "160" },
            new Shading { Val = ShadingPatternValues.Clear, Fill = "F1F3F4" }));

        var text = code.Replace("\r\n", "\n").Replace('\r', '\n');
        var lines = text.Split('\n');
        for (var i = 0; i < lines.Length; i++)
        {
            if (i > 0)
            {
                paragraph.Append(new Run(new Break()));
            }

            paragraph.Append(new Run(
                new RunProperties(
                    new RunFonts { Ascii = "Consolas", HighAnsi = "Consolas", EastAsia = "Consolas" },
                    new FontSize { Val = "18" }),
                new Text(lines[i]) { Space = SpaceProcessingModeValues.Preserve }));
        }

        return paragraph;
    }

    private static Dw.Table CreateTable(MdTableBlock table)
    {
        var tbl = new Dw.Table(
            new TableProperties(
                new TableWidth { Width = "5000", Type = TableWidthUnitValues.Pct },
                new TableBorders(
                    new TopBorder { Val = BorderValues.Single, Size = 4, Color = "C5CAD1" },
                    new BottomBorder { Val = BorderValues.Single, Size = 4, Color = "C5CAD1" },
                    new LeftBorder { Val = BorderValues.Single, Size = 4, Color = "C5CAD1" },
                    new RightBorder { Val = BorderValues.Single, Size = 4, Color = "C5CAD1" },
                    new InsideHorizontalBorder { Val = BorderValues.Single, Size = 4, Color = "C5CAD1" },
                    new InsideVerticalBorder { Val = BorderValues.Single, Size = 4, Color = "C5CAD1" })));

        for (var r = 0; r < table.Rows.Count; r++)
        {
            var row = new TableRow();
            var isHeader = table.HasHeader && r == 0;
            foreach (var cellInlines in table.Rows[r])
            {
                var cell = new TableCell(
                    new TableCellProperties(
                        new TableCellWidth { Type = TableWidthUnitValues.Auto },
                        isHeader
                            ? new Shading { Val = ShadingPatternValues.Clear, Fill = "E8EAED" }
                            : new Shading { Val = ShadingPatternValues.Clear, Fill = "FFFFFF" }),
                    CreateParagraph(cellInlines, bold: isHeader, fontSizeHalfPoints: 20, spaceAfter: 40));
                row.Append(cell);
            }

            tbl.Append(row);
        }

        return tbl;
    }

    private static Run CreateRun(MdInline inline, bool forceBold, bool forceItalic, int fontSizeHalfPoints)
    {
        var props = new RunProperties(new FontSize { Val = fontSizeHalfPoints.ToString() });
        if (forceBold || inline.Bold)
        {
            props.Append(new Bold());
        }

        if (forceItalic || inline.Italic)
        {
            props.Append(new Italic());
        }

        if (inline.Code)
        {
            props.Append(new RunFonts { Ascii = "Consolas", HighAnsi = "Consolas", EastAsia = "Consolas" });
            props.Append(new Dw.Shading { Val = ShadingPatternValues.Clear, Fill = "F1F3F4" });
        }

        if (!string.IsNullOrWhiteSpace(inline.LinkUrl))
        {
            props.Append(new Color { Val = "1A73E8" });
            props.Append(new Underline { Val = UnderlineValues.Single });
        }

        var run = new Run(props);
        var text = inline.Text ?? "";
        if (text.Contains('\n'))
        {
            var parts = text.Split('\n');
            for (var i = 0; i < parts.Length; i++)
            {
                if (i > 0)
                {
                    run.Append(new Break());
                }

                run.Append(new Text(parts[i]) { Space = SpaceProcessingModeValues.Preserve });
            }
        }
        else
        {
            run.Append(new Text(text) { Space = SpaceProcessingModeValues.Preserve });
        }

        return run;
    }
}
