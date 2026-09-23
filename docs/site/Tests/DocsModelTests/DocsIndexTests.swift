// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Testing
@testable import DocsModel

private func sym(_ id: String, _ kind: String, _ name: String, _ qualified: String,
                 parent: String? = nil, lang: String = "cpp", doc: String = "",
                 line: Int = 1) -> SymbolRecord {
    SymbolRecord(id: id, lang: lang, kind: kind, name: name, qualified: qualified,
                 parent: parent, doc: doc, doc_html: doc.isEmpty ? "" : "<p>\(doc)</p>\n",
                 file: "include/x.hpp", line: line)
}

private let sample = ApiFile(symbols: [
    sym("cpp:nprpc::PoaBuilder", "class", "PoaBuilder", "nprpc::PoaBuilder",
        doc: "Builds a <code>Poa</code>.", line: 10),
    sym("cpp:nprpc::PoaBuilder::build()", "method", "build", "nprpc::PoaBuilder::build",
        parent: "cpp:nprpc::PoaBuilder", doc: "Returns the <code>Poa</code>; see <code>PoaBuilder</code>.", line: 12),
    sym("cpp:nprpc::PoaBuilder::PoaBuilder(Rpc *)", "constructor", "PoaBuilder",
        "nprpc::PoaBuilder::PoaBuilder", parent: "cpp:nprpc::PoaBuilder", line: 11),
    sym("cpp:nprpc::Poa", "class", "Poa", "nprpc::Poa", line: 30),
    sym("cpp:nprpc::make(int)", "function", "make", "nprpc::make", line: 40),
    sym("cpp:nprpc::make(double)", "function", "make", "nprpc::make", line: 41),
    sym("cpp:nprpc::flat::Span", "struct", "Span", "nprpc::flat::Span", line: 50),
    sym("swift:NPRPC.Poa", "class", "Poa", "NPRPC.Poa", lang: "swift"),
    sym("swift:NPRPC.Poa.activate(_:)", "method", "activate", "NPRPC.Poa.activate",
        parent: "swift:NPRPC.Poa", lang: "swift",
        doc: "In a <pre><code>Poa</code></pre> block, and <a href=\"x\"><code>Poa</code></a>."),
    sym("swift:NPRPC.Orphan", "method", "orphan", "NPRPC.Ext.orphan",
        parent: "swift:Foundation.Data", lang: "swift"),
], guides: [
    GuideRecord(slug: "POA", title: "POA", html: "<p>See <a href=\"BUILD.md#deps\">build</a> and <code>PoaBuilder</code>.</p>"),
    GuideRecord(slug: "BUILD", title: "Build", html: "<p>x</p>"),
])

@Test func pagesForTopLevelAndContainers() {
    let index = DocsIndex(sample)
    #expect(index.page(lang: "cpp", components: ["nprpc", "PoaBuilder"]) != nil)
    // Members live on their parent's page, under an anchor.
    #expect(!index.hasPage("cpp:nprpc::PoaBuilder::build()"))
    #expect(index.url(of: "cpp:nprpc::PoaBuilder::build()") == "/api/cpp/nprpc/PoaBuilder#build")
    #expect(index.url(of: "cpp:nprpc::PoaBuilder") == "/api/cpp/nprpc/PoaBuilder")
}

@Test func overloadsShareAPageWithDistinctAnchors() {
    let index = DocsIndex(sample)
    let page = index.page(lang: "cpp", components: ["nprpc", "make"])
    #expect(page?.symbolIds == ["cpp:nprpc::make(int)", "cpp:nprpc::make(double)"])
    #expect(index.url(of: "cpp:nprpc::make(int)") == "/api/cpp/nprpc/make#make")
    #expect(index.url(of: "cpp:nprpc::make(double)") == "/api/cpp/nprpc/make#make-2")
}

@Test func membersSortConstructorsFirst() {
    let index = DocsIndex(sample)
    #expect(index.members(of: "cpp:nprpc::PoaBuilder").map(\.kind) == ["constructor", "method"])
}

@Test func missingParentMakesSymbolTopLevel() {
    let index = DocsIndex(sample)
    #expect(index.symbols["swift:NPRPC.Orphan"]?.parent == nil)
    #expect(index.hasPage("swift:NPRPC.Orphan"))
}

@Test func crossLinksResolveWithinLanguage() {
    let index = DocsIndex(sample)
    let doc = index.symbols["cpp:nprpc::PoaBuilder::build()"]!.doc_html
    // `Poa` is ambiguous across languages but unique within C++.
    #expect(doc.contains("<a class=\"xref\" href=\"/api/cpp/nprpc/Poa\"><code>Poa</code></a>"))
    #expect(doc.contains("<a class=\"xref\" href=\"/api/cpp/nprpc/PoaBuilder\"><code>PoaBuilder</code></a>"))
    // A symbol does not link to itself.
    let own = index.symbols["cpp:nprpc::PoaBuilder"]!.doc_html
    #expect(own.contains("href=\"/api/cpp/nprpc/Poa\""))
}

@Test func crossLinksSkipCodeBlocksAndExistingLinks() {
    let index = DocsIndex(sample)
    let doc = index.symbols["swift:NPRPC.Poa.activate(_:)"]!.doc_html
    #expect(!doc.contains("xref"))
}

@Test func guideLinksAndReferences() {
    let index = DocsIndex(sample)
    let html = index.guide(slug: "POA")!.html
    #expect(html.contains("href=\"/guide/BUILD#deps\""))
    // Unique across all languages, so guides can link it too.
    #expect(html.contains("href=\"/api/cpp/nprpc/PoaBuilder\""))
}

@Test func resolveQualifiedAndCallForms() {
    let index = DocsIndex(sample)
    #expect(index.resolve("PoaBuilder::build()", lang: "cpp") == "cpp:nprpc::PoaBuilder::build()")
    #expect(index.resolve("flat::Span", lang: "cpp") == "cpp:nprpc::flat::Span")
    #expect(index.resolve("Poa", lang: nil) == nil) // C++ and Swift both have one
    #expect(index.resolve("not an identifier", lang: "cpp") == nil)
}

@Test func searchRanksExactThenPrefix() {
    let index = DocsIndex(sample)
    let hits = index.search("poa").map(\.symbol.id)
    #expect(hits.first == "cpp:nprpc::Poa" || hits.first == "swift:NPRPC.Poa")
    #expect(hits.firstIndex(of: "cpp:nprpc::PoaBuilder")! < hits.firstIndex(of: "cpp:nprpc::PoaBuilder::build()")!)
    #expect(index.search("   ").isEmpty)
}

@Test func topLevelGroupsByNamespace() {
    let index = DocsIndex(sample)
    let groups = index.topLevelPages(lang: "cpp")
    #expect(groups.map(\.namespace) == ["nprpc", "nprpc::flat"])
    #expect(groups[0].pages.map(\.title) == ["make", "Poa", "PoaBuilder"])
}
