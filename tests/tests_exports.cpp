/*
 * tests_exports.cpp
 *
 * Copyright 2009-2026
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ct_app.h"
#include "ct_misc_utils.h"
#include "tests_common.h"

class TestCtApp : public CtApp
{
public:
    TestCtApp()
     : CtApp{"_test_exports", Gio::APPLICATION_NON_UNIQUE}
    {
        _no_gui = true;
        _on_startup(); // so that _uCtTmp is ready straight away
        // Override style schemes from the user's config so the exported HTML
        // (syntax-highlighted code) matches the expected reference regardless
        // of the local environment.
        _pCtConfig->coStyleScheme = "cobalt-darkened";
        _pCtConfig->rtStyleScheme = "classic";
        _pCtConfig->ptStyleScheme = "classic";
        _pCtConfig->taStyleScheme = "classic";
    }
    CtTmp* getCtTmp() { return _uCtTmp.get(); }
    void register_args(const std::vector<std::string>* pVecArgs) { _pVecArgs = pVecArgs; }

private:
    void on_activate() final;

    const std::vector<std::string>* _pVecArgs{nullptr};
};

void TestCtApp::on_activate()
{
    // NOTE: on windows/msys2 unit tests the passed arguments do not work so we end up here
    ASSERT_TRUE(_pVecArgs);
    if (_pVecArgs->at(2) == "--export_to_txt_dir") {
        _export_to_txt_dir = _pVecArgs->at(3);
    }
    else if (_pVecArgs->at(2) == "--export_to_pdf_dir") {
        _export_to_pdf_dir = _pVecArgs->at(3);
    }
    else if (_pVecArgs->at(2) == "--export_to_html_dir") {
        _export_to_html_dir = _pVecArgs->at(3);
    }
    if (std::find(_pVecArgs->begin(), _pVecArgs->end(), "--export_single_file") != _pVecArgs->end()) {
        _export_single_file = true;
    }
    Glib::RefPtr<Gio::File> rFile =  Gio::File::create_for_path(_pVecArgs->at(1));
    Gio::Application::type_vec_files files{rFile};
    on_open(files, "");
}

enum class ExportType { None, Txt, Pdf, Html };

class ExportsMultipleParametersTests : public ::testing::TestWithParam<std::tuple<std::string, std::string>>
{
};

TEST_P(ExportsMultipleParametersTests, ChecksExports)
{
    TestCtApp testCtApp{};
    const std::string inDocPath = std::get<0>(GetParam());
    const std::string exportSwitch = std::get<1>(GetParam());
    fs::path tmpDirpath = testCtApp.getCtTmp()->getHiddenDirPath("UT");
    fs::path tmpFilepath;
    ExportType exportType{ExportType::None};
    if (exportSwitch.find("txt") != std::string::npos) {
        exportType = ExportType::Txt;
        tmpFilepath = tmpDirpath / (Glib::path_get_basename(inDocPath)+".txt");
    }
    else if (exportSwitch.find("pdf") != std::string::npos) {
        exportType = ExportType::Pdf;
        tmpFilepath = tmpDirpath / (Glib::path_get_basename(inDocPath)+".pdf");
    }
    else if (exportSwitch.find("html") != std::string::npos) {
        exportType = ExportType::Html;
        tmpFilepath = tmpDirpath / (Glib::path_get_basename(inDocPath)+"_HTML") / "index.html";
    }
    ASSERT_FALSE(ExportType::None == exportType);
    const std::vector<std::string> vec_args{"cherrytree", inDocPath, exportSwitch, tmpDirpath.string(), "--export_single_file"};
    testCtApp.register_args(&vec_args);
    gchar** pp_args = CtStrUtil::vector_to_array(vec_args);
    testCtApp.run(vec_args.size(), pp_args);
    ASSERT_TRUE(fs::is_regular_file(tmpFilepath));
    if (ExportType::Txt == exportType) {
        std::string expectTxt_path{Glib::build_filename(UT::unitTestsDataDir, "test.export.txt")};
        std::string expectTxt = inDocPath == UT::testMultiFileSourCherry ? "# ChatGPT\nPARSING ERROR\n\n" : Glib::file_get_contents(expectTxt_path);
        std::string resultTxt = Glib::file_get_contents(tmpFilepath.string());
        ASSERT_FALSE(resultTxt.empty());
        //g_file_set_contents(Glib::build_filename(UT::unitTestsDataDir, "test.export.txtt").c_str(), resultTxt.c_str(), -1, NULL);
#if defined(_WIN32)
        ASSERT_STREQ(str::replace(expectTxt, "\n", "\r\n").c_str(), resultTxt.c_str());
#else
        ASSERT_STREQ(expectTxt.c_str(), resultTxt.c_str());
#endif
    }
    else if (ExportType::Pdf == exportType) {
        ASSERT_NE(0, fs::file_size(tmpFilepath));
    }
    else if (ExportType::Html == exportType) {
        std::string expectHtml_path{Glib::build_filename(UT::unitTestsDataDir, "test.export.html")};
        std::string expectHtml = str::replace(Glib::file_get_contents(expectHtml_path),
                                              "_REPLACE_TITLE_",
                                              Glib::path_get_basename(inDocPath));
        std::string resultHtml = Glib::file_get_contents(tmpFilepath.string());
        ASSERT_FALSE(resultHtml.empty());
        //g_file_set_contents(Glib::build_filename(UT::unitTestsDataDir, "test.export.htmll").c_str(), resultHtml.c_str(), -1, NULL);
        ASSERT_STREQ(expectHtml.c_str(), resultHtml.c_str());
    }
    if (ExportType::Html != exportType) {
        ASSERT_TRUE(fs::remove(tmpFilepath));
        ASSERT_FALSE(fs::is_regular_file(tmpFilepath));
    }
    g_strfreev(pp_args);
}

INSTANTIATE_TEST_CASE_P(
        ExportsTests,
        ExportsMultipleParametersTests,
        ::testing::Values(
            std::make_tuple(UT::testCtbDocPath, "--export_to_txt_dir"),
            std::make_tuple(UT::testCtdDocPath, "--export_to_txt_dir"),
            std::make_tuple(UT::testCtbDocPath, "--export_to_pdf_dir"),
            std::make_tuple(UT::testCtdDocPath, "--export_to_pdf_dir"),
            std::make_tuple(UT::testCtbDocPath, "--export_to_html_dir"),
            std::make_tuple(UT::testCtdDocPath, "--export_to_html_dir"),
            std::make_tuple(UT::testMultiFileSourCherry, "--export_to_txt_dir"))
);

// Rich-table specific export tests. The fixture
// (tests/data_данные/test_rich_table.ctd) contains a single rich table that
// exercises the features that needed fixing in the export code:
//
//   - per-cell custom border colour          (cell_border_colors=2,2:#e01b24)
//   - custom table border / background       (border_width=6, table_bg_color=#ffffff)
//   - per-cell background colours            (cell_bg_colors map)
//   - explicit row min height                (row_height_default=200)
//   - default horizontal alignment           (halign_default=right)
//   - default vertical alignment             (valign_default=middle)
//   - default wrap-off + per-cell wrap on    (wrap_default=0, cell_wrap=1,0:1)
//   - inline image followed by text          (image at offset 0 + text)
//   - rich-text bold span inside a cell      (<b>inside</b>)
//   - explicit per-column widths             (col_widths=200,200,200)
//
// HTML export is verified by string matching on the resulting markup so that
// every fix has a regression. PDF export is verified by checking the file is
// non-empty (PDF is binary, content checks belong to a separate visual review).

namespace {

std::string runRichTableExport(TestCtApp& testCtApp, const std::string& exportSwitch, fs::path& outFilepath)
{
    fs::path tmpDirpath = testCtApp.getCtTmp()->getHiddenDirPath("UT");
    if (exportSwitch.find("html") != std::string::npos) {
        outFilepath = tmpDirpath /
            (Glib::path_get_basename(UT::testRichTableDocPath)+"_HTML") /
            "rich_table_test_1.html"; // node "rich_table_test" with unique_id 1
    }
    else { // pdf
        outFilepath = tmpDirpath /
            (Glib::path_get_basename(UT::testRichTableDocPath)+".pdf");
    }
    const std::vector<std::string> vec_args{
        "cherrytree", UT::testRichTableDocPath, exportSwitch,
        tmpDirpath.string()};
    testCtApp.register_args(&vec_args);
    gchar** pp_args = CtStrUtil::vector_to_array(vec_args);
    testCtApp.run(vec_args.size(), pp_args);
    g_strfreev(pp_args);
    if (!fs::is_regular_file(outFilepath)) return {};
    return Glib::file_get_contents(outFilepath.string());
}

} // namespace

TEST(RichTableHtmlExportTests, PreservesRichTableFeatures)
{
    TestCtApp testCtApp{};
    fs::path htmlFilepath;
    const std::string html = runRichTableExport(testCtApp, "--export_to_html_dir", htmlFilepath);
    ASSERT_FALSE(html.empty()) << "HTML export produced no content";

    // 1. <colgroup> with explicit per-column widths preserves the editor's
    //    column geometry instead of letting the browser auto-size from content.
    ASSERT_NE(std::string::npos, html.find(R"(<colgroup><col style="width: 200px;"/><col style="width: 200px;"/><col style="width: 200px;"/></colgroup>)"))
        << "<colgroup> with explicit per-column px widths missing";

    // 2. Row min height emitted on <tr style="height: …px;"> for each row.
    ASSERT_NE(std::string::npos, html.find(R"(<tr style="height: 200px;">)"))
        << "row min height missing on <tr>";

    // 3. Table-level border + collapse + bg colour all present in the table tag.
    ASSERT_NE(std::string::npos, html.find(R"(border: 6px solid #000000)"))
        << "table-level 6px black border missing";
    ASSERT_NE(std::string::npos, html.find(R"(border-collapse: collapse)"))
        << "border-collapse missing — adjacent cells will double up";
    ASSERT_NE(std::string::npos, html.find(R"(background-color: #ffffff)"))
        << "table_bg_color (#ffffff) missing";

    // 4. Per-cell custom background (orange middle column).
    ASSERT_NE(std::string::npos, html.find(R"(background-color: #ffbe6f)"))
        << "per-cell bg colour (#ffbe6f) missing";

    // 5. Per-cell border colour override (red border on cell 2,2).
    ASSERT_NE(std::string::npos, html.find(R"(border: 6px solid #e01b24)"))
        << "per-cell border colour (#e01b24 on (2,2)) missing";

    // 6. Header <th> emits an explicit background-color (transparent or the
    //    table bg) so the shipped CSS rule for `th` doesn't show a grey fill.
    //    With a per-cell or table bg already set, those cells use that colour;
    //    cells without an explicit colour get "transparent" so they don't pick
    //    up the default grey.
    ASSERT_NE(std::string::npos, html.find(R"(<th style=)"))
        << "<th> has no inline style — would inherit the default grey CSS rule";

    // 7. Halign right (default) is propagated to per-cell text-align.
    ASSERT_NE(std::string::npos, html.find(R"(text-align: right)"))
        << "halign_default=right not emitted as text-align: right";

    // 8. Valign middle (default).
    ASSERT_NE(std::string::npos, html.find(R"(vertical-align: middle)"))
        << "valign_default=middle not emitted as vertical-align: middle";

    // 9. Wrap-off cells produce white-space: nowrap; the wrap-on cell (1,0)
    //    must NOT have nowrap on it (it's the long text-only cell).
    ASSERT_NE(std::string::npos, html.find(R"(white-space: nowrap)"))
        << "wrap_default=0 not emitted as white-space: nowrap";
    // 9b. The text "this is something very interesting…" cell has wrap on
    //     (cell_wrap=1,0:1) so it should NOT have nowrap. Search the cell that
    //     contains its text.
    {
        const std::string longTextCell = "this is something very interesting until you realise that is not always the same thing.";
        const auto pos = html.find(longTextCell);
        ASSERT_NE(std::string::npos, pos) << "long-text cell content missing";
        // Look back to the opening <td or <th of this cell.
        const auto openTag = html.rfind('<', pos);
        ASSERT_NE(std::string::npos, openTag);
        const std::string tagAndStyle = html.substr(openTag, pos - openTag);
        ASSERT_EQ(std::string::npos, tagAndStyle.find("white-space: nowrap"))
            << "wrap-on cell (1,0) wrongly got nowrap";
    }

    // 10. Inline rich-text bold span survives inside the cell ("text inside the table").
    ASSERT_NE(std::string::npos, html.find(R"(<strong>inside</strong>)"))
        << "in-cell <strong> formatting was stripped";

    // 11. Image embedded in a cell is extracted as an <img src="images/…png"> ref.
    ASSERT_NE(std::string::npos, html.find(R"(<img src="images/)"))
        << "embedded cell image not extracted as <img src=images/...>";

    // 12. The "this is one text that expands the column" cell has the image
    //     followed by the text on the same cell — the text must be present in
    //     the cell content so the inline rendering preserves it.
    ASSERT_NE(std::string::npos, html.find("this is one text that expands the column"))
        << "wrap-off cell text dropped from the export";
}

TEST(RichTableHtmlExportTests, ImageFilesAreExtracted)
{
    TestCtApp testCtApp{};
    fs::path htmlFilepath;
    const std::string html = runRichTableExport(testCtApp, "--export_to_html_dir", htmlFilepath);
    ASSERT_FALSE(html.empty());

    // The rich table embeds 5 images: (0,1), (1,0), (1,2), (2,0), (2,2).
    // Confirm at least one was written to the images/ subdir of the export.
    fs::path imagesDir = htmlFilepath.parent_path() / "images";
    ASSERT_TRUE(fs::is_directory(imagesDir)) << "images/ subdir missing";
    bool foundPng = false;
    for (const fs::path& f : fs::get_dir_entries(imagesDir)) {
        const std::string fname = Glib::path_get_basename(f.string());
        if (Glib::str_has_suffix(fname, ".png") and Glib::str_has_prefix(fname, "1-")) {
            foundPng = true; // node id 1 prefix, embedded cell images
            break;
        }
    }
    ASSERT_TRUE(foundPng) << "no node-id-prefixed PNG was written to images/";
}

TEST(RichTableExportTests, PdfFileIsCreatedAndNonEmpty)
{
    // PDF is binary — we can't easily string-match content like HTML. This
    // exists so a regression that crashes or produces an empty PDF is caught.
    TestCtApp testCtApp{};
    fs::path pdfFilepath;
    const std::string pdfContent = runRichTableExport(testCtApp, "--export_to_pdf_dir", pdfFilepath);
    ASSERT_TRUE(fs::is_regular_file(pdfFilepath));
    ASSERT_GT(fs::file_size(pdfFilepath), 0u);
    // PDF magic header — a file that doesn't start with %PDF- isn't a PDF.
    ASSERT_GE(pdfContent.size(), 4u);
    ASSERT_EQ("%PDF", pdfContent.substr(0, 4));
    ASSERT_TRUE(fs::remove(pdfFilepath));
}
