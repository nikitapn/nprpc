// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Foundation
import Mustache

/// Loads a directory of `.mustache` files and renders from it.
///
/// swift-mustache ships `MustacheLibrary(directory:)`, but it is `async` (so it
/// cannot be called from the server's synchronous bootstrap) and its `reload:`
/// flag is compiled out of release builds and trips a `preconditionFailure` on
/// templates that did not come from a file.  Walking the directory here keeps
/// startup synchronous and makes hot reload work the same way in both
/// configurations.
public final class TemplateLibrary: @unchecked Sendable {
    private let directory: String
    private let hotReload: Bool
    private let lock = NSLock()
    private var library: MustacheLibrary

    /// Loads every `.mustache` file under `directory`. Throws `TemplateError`
    /// if the directory is unreadable, holds no templates, or one fails to
    /// parse.
    ///
    /// - Parameters:
    ///   - directory: root of the template tree.  A template's name is its path
    ///     below this directory without the `.mustache` extension, so
    ///     `partials/post_cards.mustache` is `{{> partials/post_cards}}`.
    ///   - hotReload: re-read the tree on every render.  A handful of small
    ///     file reads per request — fine for development, waste in production.
    public init(directory: String, hotReload: Bool = false) throws {
        self.directory = directory
        self.hotReload = hotReload
        (self.library, self.loadedNames) = try Self.load(from: directory)
    }

    /// Render `object` with the named template, or nil if no such template.
    public func render(_ object: Any, withTemplate name: String) -> String? {
        lock.lock()
        defer { lock.unlock() }

        if hotReload {
            // A template edited into a broken state should show up as a 500 on
            // the next request, not take the server's good copy down with it.
            if let reloaded = try? Self.load(from: directory) {
                (library, loadedNames) = reloaded
            }
        }
        return library.render(object, withTemplate: name)
    }

    /// Template names currently loaded, for startup logging.
    public var templateNames: [String] {
        lock.lock()
        defer { lock.unlock() }
        return loadedNames.sorted()
    }

    private var loadedNames: [String] = []

    private static func load(
        from directory: String
    ) throws -> (MustacheLibrary, [String]) {
        let root = directory.hasSuffix("/") ? directory : directory + "/"
        let fm = FileManager.default
        guard let walker = fm.enumerator(atPath: root) else {
            throw TemplateError.directoryUnreadable(directory)
        }

        var templates: [String: MustacheTemplate] = [:]
        for case let path as String in walker where path.hasSuffix(".mustache") {
            let source = try String(contentsOfFile: root + path, encoding: .utf8)
            let name = String(path.dropLast(".mustache".count))
            do {
                templates[name] = try MustacheTemplate(string: source)
            } catch {
                throw TemplateError.parseFailed(name: name, underlying: error)
            }
        }

        guard !templates.isEmpty else {
            throw TemplateError.noTemplates(directory)
        }
        return (MustacheLibrary(templates: templates), Array(templates.keys))
    }
}

/// Why a `TemplateLibrary` could not load its directory.
public enum TemplateError: Error, CustomStringConvertible {
    /// The directory does not exist or cannot be listed.
    case directoryUnreadable(String)
    /// The directory holds no `.mustache` files.
    case noTemplates(String)
    /// A template has a syntax error; `name` is its path without the extension.
    case parseFailed(name: String, underlying: Error)

    /// A message naming the directory or template at fault.
    public var description: String {
        switch self {
        case .directoryUnreadable(let dir):
            return "Cannot read template directory '\(dir)'"
        case .noTemplates(let dir):
            return "No .mustache files found under '\(dir)'"
        case .parseFailed(let name, let underlying):
            return "Template '\(name)' failed to parse: \(underlying)"
        }
    }
}
