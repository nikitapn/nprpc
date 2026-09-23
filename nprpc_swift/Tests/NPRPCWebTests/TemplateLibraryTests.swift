// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Foundation
import Testing
import NPRPCWeb

/// A template directory that is removed when the test ends.
private final class TemplateDir {
    let path: String

    init() throws {
        path = NSTemporaryDirectory() + "nprpcweb-" + UUID().uuidString
        try FileManager.default.createDirectory(atPath: path + "/partials",
                                                withIntermediateDirectories: true)
    }

    deinit { try? FileManager.default.removeItem(atPath: path) }

    func write(_ name: String, _ text: String) throws {
        try text.write(toFile: path + "/" + name + ".mustache", atomically: true, encoding: .utf8)
    }
}

private struct Page {
    let title: String
    let items: [String]
}

@Test func rendersWithNestedPartials() throws {
    let dir = try TemplateDir()
    try dir.write("page", "<h1>{{title}}</h1>{{> partials/list}}")
    try dir.write("partials/list", "{{#items}}<li>{{.}}</li>{{/items}}")

    let library = try TemplateLibrary(directory: dir.path)
    #expect(library.templateNames == ["page", "partials/list"])
    #expect(library.render(Page(title: "A & B", items: ["x", "y"]), withTemplate: "page")
            == "<h1>A &amp; B</h1><li>x</li><li>y</li>")
    #expect(library.render(Page(title: "", items: []), withTemplate: "missing") == nil)
}

@Test func hotReloadPicksUpEditsAndSurvivesBrokenTemplates() throws {
    let dir = try TemplateDir()
    try dir.write("page", "v1 {{title}}")
    let library = try TemplateLibrary(directory: dir.path, hotReload: true)
    let page = Page(title: "t", items: [])
    #expect(library.render(page, withTemplate: "page") == "v1 t")

    try dir.write("page", "v2 {{title}}")
    #expect(library.render(page, withTemplate: "page") == "v2 t")

    // A template edited into a broken state keeps serving the last good copy.
    try dir.write("page", "v3 {{#title}}")
    #expect(library.render(page, withTemplate: "page") == "v2 t")
}

@Test func withoutHotReloadEditsAreIgnored() throws {
    let dir = try TemplateDir()
    try dir.write("page", "v1")
    let library = try TemplateLibrary(directory: dir.path)
    try dir.write("page", "v2")
    #expect(library.render(Page(title: "", items: []), withTemplate: "page") == "v1")
}

@Test func reportsBadDirectoriesAndTemplates() throws {
    #expect(throws: TemplateError.self) {
        try TemplateLibrary(directory: "/nonexistent/templates")
    }
    let empty = try TemplateDir()
    #expect(throws: TemplateError.self) { try TemplateLibrary(directory: empty.path) }

    let broken = try TemplateDir()
    try broken.write("page", "{{#unclosed}}")
    #expect(throws: TemplateError.self) { try TemplateLibrary(directory: broken.path) }
}
