namespace XScope.Services;

internal static class AppPaths
{
    /// <summary>Client-chosen private data root (LocalAppData\XScope\data).</summary>
    public static string DataRoot
    {
        get
        {
            var root = System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "XScope",
                "data");
            System.IO.Directory.CreateDirectory(root);
            return root;
        }
    }
}
