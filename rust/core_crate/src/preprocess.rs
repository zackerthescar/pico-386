//! `#include` preprocessing for PICO-8 source.
//!
//! PICO-8 lets a `.p8`/`.lua` cart pull in external Lua via a line that
//! starts with `#include FILE`. The directive is line-oriented:
//!
//!   * It must appear at the start of a line (leading whitespace allowed).
//!   * The argument is a path; PICO-8 accepts `.lua`, `.p8` and `.p8.png`
//!     targets, with an optional `:N` tab selector for `.p8`/`.p8.png`.
//!   * The included file's contents are spliced in verbatim at that point.
//!   * Includes are NOT recursive (PICO-8 does not support nested imports);
//!     `#include` lines inside an included file are left untouched.
//!
//! When a cart is saved as `.p8.png` (or exported to a binary) the includes
//! are flattened at save time, so the DOS runtime never sees them — this pass
//! only matters for raw `.p8`/`.lua` source fed to the host compiler. To keep
//! the core crate `no_std`/allocator-only, file access is abstracted behind
//! the `IncludeResolver` trait; `std` builds get a filesystem resolver.

use alloc::string::String;
use alloc::vec::Vec;

/// Resolves an `#include` target path to the raw source text to splice in.
pub trait IncludeResolver {
    /// Return the contents of `path`, or `None` if it can't be read.
    fn resolve(&mut self, path: &str) -> Option<String>;
}

/// Outcome of preprocessing.
pub struct Preprocessed {
    /// The flattened source with `#include` lines expanded.
    pub source: String,
}

/// Returns true if `line` (without its trailing newline) is an `#include`
/// directive, yielding the target path argument when so.
fn parse_include_line(line: &str) -> Option<&str> {
    let trimmed = line.trim_start_matches([' ', '\t']);
    let rest = trimmed.strip_prefix("#include")?;
    // Require whitespace (or end) after the keyword so we don't match e.g.
    // `#includes_something`.
    if let Some(first) = rest.chars().next() {
        if first != ' ' && first != '\t' {
            return None;
        }
    } else {
        return None;
    }
    let arg = rest.trim_start_matches([' ', '\t']).trim_end();
    if arg.is_empty() {
        None
    } else {
        Some(arg)
    }
}

/// Expand `#include` directives in `src` using `resolver`. Non-recursive:
/// directives inside included files are left as-is (matching PICO-8). Lines
/// whose target can't be resolved are kept verbatim so compilation surfaces a
/// useful error rather than silently dropping code.
pub fn preprocess_includes<R: IncludeResolver>(src: &str, resolver: &mut R) -> Preprocessed {
    // Fast path: nothing to do.
    if !src.contains("#include") {
        return Preprocessed {
            source: String::from(src),
        };
    }

    let mut out = String::with_capacity(src.len());
    for line in split_keep_eol(src) {
        let body = line.trim_end_matches(['\r', '\n']);
        if let Some(arg) = parse_include_line(body) {
            if let Some(text) = resolver.resolve(arg) {
                out.push_str(&text);
                // Ensure included content is newline-terminated so the
                // following line starts cleanly.
                if !text.ends_with('\n') {
                    out.push('\n');
                }
                continue;
            }
        }
        out.push_str(line);
    }
    Preprocessed { source: out }
}

/// Split `s` into lines, keeping each line's terminating newline characters.
fn split_keep_eol(s: &str) -> Vec<&str> {
    let mut lines = Vec::new();
    let bytes = s.as_bytes();
    let mut start = 0;
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'\n' {
            lines.push(&s[start..=i]);
            start = i + 1;
        }
        i += 1;
    }
    if start < bytes.len() {
        lines.push(&s[start..]);
    }
    lines
}

// ── std filesystem resolver ──────────────────────────────────────────

/// Filesystem-backed include resolver rooted at a base directory. Paths are
/// resolved relative to the base and constrained to remain inside it, matching
/// PICO-8's restriction that includes live under the cart root.
#[cfg(feature = "std")]
pub struct FsIncludeResolver {
    root: std::path::PathBuf,
}

#[cfg(feature = "std")]
impl FsIncludeResolver {
    /// Create a resolver rooted at the directory containing `cart_file` (or
    /// `cart_file` itself if it's a directory).
    pub fn for_cart(cart_file: &std::path::Path) -> Self {
        let root = if cart_file.is_dir() {
            cart_file.to_path_buf()
        } else {
            cart_file
                .parent()
                .map(|p| p.to_path_buf())
                .unwrap_or_else(|| std::path::PathBuf::from("."))
        };
        let root = std::fs::canonicalize(&root).unwrap_or(root);
        FsIncludeResolver { root }
    }
}

#[cfg(feature = "std")]
impl IncludeResolver for FsIncludeResolver {
    fn resolve(&mut self, path: &str) -> Option<String> {
        // Strip an optional `:N` tab selector (only meaningful for .p8 carts;
        // for plain .lua we just read the whole file).
        let path = match path.rsplit_once(':') {
            Some((p, n)) if n.chars().all(|c| c.is_ascii_digit()) && !n.is_empty() => p,
            _ => path,
        };
        let full = self.root.join(path);
        let full = std::fs::canonicalize(&full).unwrap_or(full);
        if !full.starts_with(&self.root) {
            return None; // outside the allowed cart root
        }
        std::fs::read_to_string(&full).ok()
    }
}

// ── tests ────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::collections::BTreeMap;
    use alloc::string::ToString;

    struct MapResolver {
        files: BTreeMap<String, String>,
        seen: Vec<String>,
    }

    impl MapResolver {
        fn new() -> Self {
            MapResolver {
                files: BTreeMap::new(),
                seen: Vec::new(),
            }
        }
        fn with(mut self, name: &str, body: &str) -> Self {
            self.files.insert(name.to_string(), body.to_string());
            self
        }
    }

    impl IncludeResolver for MapResolver {
        fn resolve(&mut self, path: &str) -> Option<String> {
            self.seen.push(path.to_string());
            self.files.get(path).cloned()
        }
    }

    #[test]
    fn no_include_is_passthrough() {
        let mut r = MapResolver::new();
        let pp = preprocess_includes("x=1\ny=2\n", &mut r);
        assert_eq!(pp.source, "x=1\ny=2\n");
        assert!(r.seen.is_empty());
    }

    #[test]
    fn basic_include_splices_content() {
        let mut r = MapResolver::new().with("lib.lua", "function f() end\n");
        let pp = preprocess_includes("#include lib.lua\ng()\n", &mut r);
        assert_eq!(pp.source, "function f() end\ng()\n");
        assert_eq!(r.seen, ["lib.lua"]);
    }

    #[test]
    fn include_with_leading_whitespace() {
        let mut r = MapResolver::new().with("a.lua", "a=1");
        let pp = preprocess_includes("  \t#include a.lua\nb=2\n", &mut r);
        // Missing trailing newline in included file is added.
        assert_eq!(pp.source, "a=1\nb=2\n");
    }

    #[test]
    fn include_not_recursive() {
        // An #include inside an included file is left untouched.
        let mut r = MapResolver::new()
            .with("a.lua", "#include b.lua\nx=1\n")
            .with("b.lua", "y=2\n");
        let pp = preprocess_includes("#include a.lua\n", &mut r);
        assert_eq!(pp.source, "#include b.lua\nx=1\n");
        assert_eq!(r.seen, ["a.lua"]);
    }

    #[test]
    fn unresolved_include_kept_verbatim() {
        let mut r = MapResolver::new();
        let pp = preprocess_includes("#include missing.lua\nz=3\n", &mut r);
        assert_eq!(pp.source, "#include missing.lua\nz=3\n");
    }

    #[test]
    fn tab_selector_stripped_by_resolver_arg() {
        // The raw arg (with :N) is what the resolver receives; trimming the
        // tab selector is the resolver's job. Here we just confirm the arg is
        // passed through unmodified.
        let mut r = MapResolver::new().with("c.p8:2", "tab2\n");
        let pp = preprocess_includes("#include c.p8:2\n", &mut r);
        assert_eq!(pp.source, "tab2\n");
        assert_eq!(r.seen, ["c.p8:2"]);
    }

    #[test]
    fn hash_other_directive_not_treated_as_include() {
        let mut r = MapResolver::new();
        let pp = preprocess_includes("#includes_not_a_keyword foo\n", &mut r);
        assert_eq!(pp.source, "#includes_not_a_keyword foo\n");
        assert!(r.seen.is_empty());
    }

    #[test]
    fn include_no_trailing_newline_on_last_line() {
        let mut r = MapResolver::new().with("d.lua", "d=4\n");
        let pp = preprocess_includes("#include d.lua", &mut r);
        assert_eq!(pp.source, "d=4\n");
    }
}
