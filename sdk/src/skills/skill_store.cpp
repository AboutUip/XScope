#include "xscope/skills/skill_store.hpp"

#include "xscope/utils/utils.hpp"
#include "xscope/xaiop/bridge.hpp"

#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace xscope::skills {

void SkillStore::open(const fs::path& skills_root) {
    if (skills_root.empty()) {
        throw SkillError("skills_root is empty");
    }
    xscope::utils::ensure_directory(skills_root);
    root_ = fs::absolute(skills_root);
    reload();
}

void SkillStore::close() noexcept {
    root_.clear();
    index_.clear();
}

void SkillStore::reload() {
    if (root_.empty()) {
        throw SkillError("skill store is not open");
    }
    std::vector<SkillInfo> next;
    for (const auto& entry : fs::directory_iterator(root_)) {
        if (entry.is_directory()) {
            const auto skill_md = entry.path() / "SKILL.md";
            if (!fs::exists(skill_md) || !fs::is_regular_file(skill_md)) {
                continue;
            }
            next.push_back(make_info_from_path(skill_md, entry.path().filename().string(), true));
        } else if (entry.is_regular_file()) {
            const auto ext = entry.path().extension().string();
            if (ext != ".md" && ext != ".MD") {
                continue;
            }
            const auto stem = entry.path().stem().string();
            if (stem == "README" || stem == "readme") {
                continue;
            }
            next.push_back(make_info_from_path(entry.path(), stem, false));
        }
    }
    std::sort(next.begin(), next.end(),
              [](const SkillInfo& a, const SkillInfo& b) { return a.id < b.id; });
    index_ = std::move(next);
}

std::vector<SkillInfo> SkillStore::list() const { return index_; }

std::optional<SkillInfo> SkillStore::find(const std::string& id) const {
    for (const auto& info : index_) {
        if (info.id == id) {
            return info;
        }
    }
    return std::nullopt;
}

SkillDocument SkillStore::load(const std::string& id) const {
    auto info = find(id);
    if (!info) {
        throw SkillError("skill not found: " + id);
    }
    SkillDocument doc;
    doc.info = *info;
    try {
        doc.raw = xscope::utils::read_file_utf8(info->path);
    } catch (const std::exception& ex) {
        throw SkillError(ex.what());
    }
    parse_frontmatter(doc.raw, doc.frontmatter, doc.body, doc.info.name, doc.info.description);
    if (doc.info.name.empty()) {
        doc.info.name = doc.info.id;
    }
    return doc;
}

std::string SkillStore::install(const fs::path& source) {
    if (root_.empty()) {
        throw SkillError("skill store is not open");
    }
    if (!fs::exists(source)) {
        throw SkillError("install source does not exist");
    }

    if (fs::is_directory(source)) {
        const auto skill_md = source / "SKILL.md";
        if (!fs::exists(skill_md)) {
            throw SkillError("skill directory must contain SKILL.md");
        }
        const std::string id = xscope::utils::sanitize_id(source.filename().string());
        const auto dest = root_ / id;
        if (fs::exists(dest)) {
            fs::remove_all(dest);
        }
        fs::copy(source, dest, fs::copy_options::recursive);
        reload();
        return id;
    }

    if (fs::is_regular_file(source)) {
        const auto ext = source.extension().string();
        if (ext != ".md" && ext != ".MD") {
            throw SkillError("single-file skill must be a .md file");
        }
        const std::string id = xscope::utils::sanitize_id(source.stem().string());
        const auto dest_dir = root_ / id;
        xscope::utils::ensure_directory(dest_dir);
        fs::copy_file(source, dest_dir / "SKILL.md", fs::copy_options::overwrite_existing);
        const auto loose = root_ / (id + ".md");
        if (fs::exists(loose)) {
            fs::remove(loose);
        }
        reload();
        return id;
    }

    throw SkillError("unsupported skill source type");
}

void SkillStore::save(const std::string& id, const std::string& raw_markdown) {
    if (root_.empty()) {
        throw SkillError("skill store is not open");
    }
    const std::string safe = xscope::utils::sanitize_id(id);
    if (safe.empty()) {
        throw SkillError("invalid skill id");
    }
    const auto dest = root_ / safe / "SKILL.md";
    try {
        xscope::utils::write_file_utf8(dest, raw_markdown);
    } catch (const std::exception& ex) {
        throw SkillError(ex.what());
    }
    const auto loose = root_ / (safe + ".md");
    if (fs::exists(loose)) {
        fs::remove(loose);
    }
    reload();
}

void SkillStore::remove(const std::string& id) {
    if (root_.empty()) {
        throw SkillError("skill store is not open");
    }
    auto info = find(id);
    if (!info) {
        throw SkillError("skill not found: " + id);
    }
    if (info->is_directory) {
        fs::remove_all(info->path.parent_path());
    } else {
        fs::remove(info->path);
    }
    reload();
}

std::string SkillStore::catalog_xaiop() const {
    std::ostringstream json;
    json << "{\"meta\":{\"kind\":\"skill_catalog\",\"schema\":1},\"skills\":{";
    for (size_t i = 0; i < index_.size(); ++i) {
        const auto& s = index_[i];
        if (i) {
            json << ',';
        }
        json << '"' << xscope::utils::json_escape(s.id) << "\":{"
             << "\"name\":\"" << xscope::utils::json_escape(s.name) << "\","
             << "\"description\":\"" << xscope::utils::json_escape(s.description) << "\","
             << "\"updated_at\":" << s.updated_at << ','
             << "\"is_directory\":" << (s.is_directory ? "true" : "false") << '}';
    }
    json << "}}";
    return xscope::xaiop::Bridge::instance().encode_json(json.str());
}

bool SkillStore::parse_frontmatter(const std::string& raw, std::string& frontmatter, std::string& body,
                                   std::string& name, std::string& description) {
    frontmatter.clear();
    body.clear();
    name.clear();
    description.clear();

    if (!xscope::utils::starts_with(raw, "---")) {
        body = raw;
        return false;
    }
    const auto second = raw.find("\n---", 3);
    if (second == std::string::npos) {
        body = raw;
        return false;
    }
    std::size_t fm_start = 3;
    if (fm_start < raw.size() && raw[fm_start] == '\r') {
        ++fm_start;
    }
    if (fm_start < raw.size() && raw[fm_start] == '\n') {
        ++fm_start;
    }
    frontmatter = raw.substr(fm_start, second - fm_start);
    std::size_t body_start = second + 4;
    if (body_start < raw.size() && raw[body_start] == '\r') {
        ++body_start;
    }
    if (body_start < raw.size() && raw[body_start] == '\n') {
        ++body_start;
    }
    body = raw.substr(body_start);

    std::istringstream iss(frontmatter);
    std::string line;
    bool in_description = false;
    std::ostringstream desc;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (in_description) {
            if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
                if (desc.tellp() > 0) {
                    desc << ' ';
                }
                desc << xscope::utils::trim_copy(line);
                continue;
            }
            in_description = false;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = xscope::utils::trim_copy(line.substr(0, colon));
        std::string value = xscope::utils::trim_copy(line.substr(colon + 1));
        if (key == "name") {
            name = xscope::utils::strip_quotes(value);
        } else if (key == "description") {
            if (value == ">" || value == "|-" || value == ">-" || value == "|") {
                in_description = true;
                desc.str("");
                desc.clear();
            } else {
                description = xscope::utils::strip_quotes(value);
            }
        }
    }
    if (in_description || desc.tellp() > 0) {
        const auto d = xscope::utils::trim_copy(desc.str());
        if (!d.empty()) {
            description = d;
        }
    }
    return true;
}

SkillInfo SkillStore::make_info_from_path(const fs::path& skill_md, const std::string& id,
                                          bool is_directory) const {
    SkillInfo info;
    info.id = id;
    info.path = fs::absolute(skill_md);
    info.is_directory = is_directory;
    info.updated_at = xscope::utils::file_mtime_unix_seconds(skill_md);
    try {
        const auto raw = xscope::utils::read_file_utf8(skill_md);
        std::string fm;
        std::string body;
        parse_frontmatter(raw, fm, body, info.name, info.description);
    } catch (...) {
    }
    if (info.name.empty()) {
        info.name = info.id;
    }
    return info;
}

} // namespace xscope::skills
