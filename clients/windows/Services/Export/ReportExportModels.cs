namespace XScope.Services.Export;

internal enum ReportExportFormat
{
    Markdown,
    Pdf,
    Docx,
}

internal sealed class MdInline
{
    public required string Text { get; init; }
    public bool Bold { get; init; }
    public bool Italic { get; init; }
    public bool Code { get; init; }
    public string? LinkUrl { get; init; }
}

internal abstract class MdBlock;

internal sealed class MdHeadingBlock : MdBlock
{
    public required int Level { get; init; }
    public required IReadOnlyList<MdInline> Inlines { get; init; }
}

internal sealed class MdParagraphBlock : MdBlock
{
    public required IReadOnlyList<MdInline> Inlines { get; init; }
}

internal sealed class MdListBlock : MdBlock
{
    public required bool Ordered { get; init; }
    public required IReadOnlyList<MdListItem> Items { get; init; }
}

internal sealed class MdListItem
{
    public required IReadOnlyList<MdInline> Inlines { get; init; }
}

internal sealed class MdCodeBlock : MdBlock
{
    public string? Language { get; init; }
    public required string Code { get; init; }
}

internal sealed class MdQuoteBlock : MdBlock
{
    public required IReadOnlyList<MdInline> Inlines { get; init; }
}

internal sealed class MdTableBlock : MdBlock
{
    public required IReadOnlyList<IReadOnlyList<IReadOnlyList<MdInline>>> Rows { get; init; }
    public bool HasHeader { get; init; }
}

internal sealed class MdThematicBreakBlock : MdBlock;

internal sealed class MdReportDocument
{
    public required string Title { get; init; }
    public required IReadOnlyList<MdBlock> Blocks { get; init; }
}
