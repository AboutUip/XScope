using Markdig;
using Markdig.Extensions.Tables;
using Markdig.Syntax;
using Markdig.Syntax.Inlines;

namespace XScope.Services.Export;

internal static class MarkdownBlockParser
{
    private static readonly MarkdownPipeline Pipeline = new MarkdownPipelineBuilder()
        .UseAdvancedExtensions()
        .Build();

    public static MdReportDocument Parse(string markdown, string fallbackTitle)
    {
        var text = Normalize(markdown);
        var doc = Markdig.Markdown.Parse(text, Pipeline);
        var blocks = new List<MdBlock>();
        string? title = null;

        foreach (var block in doc)
        {
            if (block is HeadingBlock heading && heading.Level == 1 && title is null)
            {
                title = InlineText(heading.Inline);
            }

            ConvertBlock(block, blocks);
        }

        if (string.IsNullOrWhiteSpace(title))
        {
            title = string.IsNullOrWhiteSpace(fallbackTitle) ? "Research report" : fallbackTitle.Trim();
        }

        return new MdReportDocument
        {
            Title = title,
            Blocks = blocks,
        };
    }

    private static string Normalize(string markdown)
    {
        if (string.IsNullOrEmpty(markdown))
        {
            return "";
        }

        return markdown.Replace("\r\n", "\n").Replace('\r', '\n').Trim() + "\n";
    }

    private static void ConvertBlock(Block block, List<MdBlock> sink)
    {
        switch (block)
        {
            case HeadingBlock heading:
                sink.Add(new MdHeadingBlock
                {
                    Level = Math.Clamp(heading.Level, 1, 6),
                    Inlines = CollectInlines(heading.Inline),
                });
                break;

            case ParagraphBlock paragraph:
                sink.Add(new MdParagraphBlock { Inlines = CollectInlines(paragraph.Inline) });
                break;

            case QuoteBlock quote:
            {
                var inlines = new List<MdInline>();
                foreach (var child in quote)
                {
                    if (child is LeafBlock leaf && leaf.Inline is not null)
                    {
                        if (inlines.Count > 0)
                        {
                            inlines.Add(new MdInline { Text = " " });
                        }

                        inlines.AddRange(CollectInlines(leaf.Inline));
                    }
                }

                sink.Add(new MdQuoteBlock { Inlines = inlines });
                break;
            }

            case FencedCodeBlock fenced:
                sink.Add(new MdCodeBlock
                {
                    Language = string.IsNullOrWhiteSpace(fenced.Info) ? null : fenced.Info.Trim(),
                    Code = fenced.Lines.ToString() ?? "",
                });
                break;

            case CodeBlock code:
                sink.Add(new MdCodeBlock
                {
                    Language = null,
                    Code = code.Lines.ToString() ?? "",
                });
                break;

            case ListBlock list:
            {
                var items = new List<MdListItem>();
                foreach (var item in list.OfType<ListItemBlock>())
                {
                    var inlines = new List<MdInline>();
                    foreach (var child in item)
                    {
                        if (child is LeafBlock leaf && leaf.Inline is not null)
                        {
                            if (inlines.Count > 0)
                            {
                                inlines.Add(new MdInline { Text = " " });
                            }

                            inlines.AddRange(CollectInlines(leaf.Inline));
                        }
                    }

                    items.Add(new MdListItem { Inlines = inlines });
                }

                sink.Add(new MdListBlock { Ordered = list.IsOrdered, Items = items });
                break;
            }

            case Table table:
            {
                var rows = new List<IReadOnlyList<IReadOnlyList<MdInline>>>();
                foreach (var row in table.OfType<TableRow>())
                {
                    var cells = new List<IReadOnlyList<MdInline>>();
                    foreach (var cell in row.OfType<TableCell>())
                    {
                        var cellInlines = new List<MdInline>();
                        foreach (var child in cell)
                        {
                            if (child is LeafBlock leaf && leaf.Inline is not null)
                            {
                                if (cellInlines.Count > 0)
                                {
                                    cellInlines.Add(new MdInline { Text = " " });
                                }

                                cellInlines.AddRange(CollectInlines(leaf.Inline));
                            }
                        }

                        cells.Add(cellInlines);
                    }

                    rows.Add(cells);
                }

                sink.Add(new MdTableBlock
                {
                    Rows = rows,
                    HasHeader = table.Count > 0 && table[0] is TableRow { IsHeader: true },
                });
                break;
            }

            case ThematicBreakBlock:
                sink.Add(new MdThematicBreakBlock());
                break;

            case ContainerBlock container:
                foreach (var child in container)
                {
                    ConvertBlock(child, sink);
                }

                break;
        }
    }

    private static IReadOnlyList<MdInline> CollectInlines(ContainerInline? root)
    {
        var list = new List<MdInline>();
        if (root is null)
        {
            return list;
        }

        WalkInline(root, list, bold: false, italic: false);
        return list;
    }

    private static void WalkInline(Inline inline, List<MdInline> sink, bool bold, bool italic)
    {
        switch (inline)
        {
            case LiteralInline lit:
                sink.Add(new MdInline { Text = lit.Content.ToString(), Bold = bold, Italic = italic });
                break;

            case CodeInline code:
                sink.Add(new MdInline { Text = code.Content, Code = true, Bold = bold, Italic = italic });
                break;

            case LineBreakInline:
                sink.Add(new MdInline { Text = "\n", Bold = bold, Italic = italic });
                break;

            case LinkInline link:
            {
                var start = sink.Count;
                if (link.FirstChild is not null)
                {
                    for (var child = link.FirstChild; child is not null; child = child.NextSibling)
                    {
                        WalkInline(child, sink, bold, italic);
                    }
                }
                else
                {
                    sink.Add(new MdInline { Text = link.Url ?? "", Bold = bold, Italic = italic });
                }

                var url = link.Url;
                if (!string.IsNullOrWhiteSpace(url))
                {
                    for (var i = start; i < sink.Count; i++)
                    {
                        var cur = sink[i];
                        sink[i] = new MdInline
                        {
                            Text = cur.Text,
                            Bold = cur.Bold,
                            Italic = cur.Italic,
                            Code = cur.Code,
                            LinkUrl = url,
                        };
                    }
                }

                break;
            }

            case EmphasisInline emphasis:
            {
                var nextBold = bold || emphasis.DelimiterCount >= 2;
                var nextItalic = italic || emphasis.DelimiterCount == 1;
                for (var child = emphasis.FirstChild; child is not null; child = child.NextSibling)
                {
                    WalkInline(child, sink, nextBold, nextItalic);
                }

                break;
            }

            case ContainerInline container:
                for (var child = container.FirstChild; child is not null; child = child.NextSibling)
                {
                    WalkInline(child, sink, bold, italic);
                }

                break;
        }

        // Markdig trees are sibling-linked under containers; root ContainerInline is walked via FirstChild above.
        // For a direct call on a non-container leaf we stop; siblings of root are handled by caller iterating FirstChild.
    }

    private static string InlineText(ContainerInline? root)
    {
        if (root is null)
        {
            return "";
        }

        var parts = CollectInlines(root);
        return string.Concat(parts.Select(p => p.Text)).Trim();
    }
}
