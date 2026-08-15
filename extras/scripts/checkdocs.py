"""Reports public API an editor would hover blank.

Parses with libclang and reads Cursor.raw_comment - the same attachment clangd
uses - so this answers "will hover show anything" rather than guessing from line
positions. Needs: pip install libclang

Overloads are judged as a group: one comment above the first of a set that
differs only by argument type is the normal C++ shape, and an editor shows it
whichever member it resolves to. A group with no comment anywhere is the defect.

Usage: python extras/checkdocs.py src/BlaeckSerial.h [-- <extra clang args>]
"""
import sys
import io
import collections
import re
import clang.cindex as ci

# Constructors are left out: the handle types are returned by the library, never
# constructed by a sketch, so nobody hovers them.
#
# Data members are in: DeviceName and its neighbours are assigned by every sketch
# and hovered like anything else, and a comment written for a group of them shows
# the group's text whichever one is hovered - the same defect the methods had.
KINDS = {
    ci.CursorKind.CXX_METHOD,
    ci.CursorKind.FUNCTION_DECL,
    ci.CursorKind.FUNCTION_TEMPLATE,
    ci.CursorKind.FIELD_DECL,
}

# A data member counts only on a type the library presents as its API - BlaeckSerial
# and the handles it returns. The record structs behind them (Signal, SignalMeta,
# CommandHandlerEntry) are public only because struct makes them so, and a sketch
# never names one.
FIELD_OWNER = "Blaeck"

NL = chr(10)

SECTION = re.compile(r"^[\s/*-]*$")

def is_doc(raw):
    """True when a comment says something, rather than marking a section.

    A divider like "// ----- Tick -----" attaches to whatever follows it, so an
    editor shows it as the documentation. Stripping the punctuation leaves a
    word or two, which is not a description of anything.
    """
    return len(_words(raw)) >= 6


def _words(raw):
    """The words a comment carries once its punctuation and dividers are stripped."""
    if not raw:
        return []
    words = []
    for line in raw.splitlines():
        line = line.strip().lstrip("/*").strip().strip("-").strip()
        if line and not SECTION.match(line):
            words.extend(line.split())
    return words


# An example is an indented code line inside the comment - the convention the
# header already uses:
#
#   // Adds a signal ...
#   //
#   //   BlaeckSerial.addSignal(F("Temperature"), &Temperature);
#
# Rust requires one on every public item, on the grounds that a signature says
# what a call looks like and only an example says why you would make it.
EXAMPLE = re.compile(r"^\s*(?://+|\*)\s{2,}\S")

def has_example(raw):
    if not raw:
        return False
    # Doxygen says so outright; the indent heuristic below is for the plain-comment
    # style the header used before the conversion.
    if "@code" in raw:
        return True
    for line in raw.splitlines():
        if not EXAMPLE.match(line):
            continue
        body = line.strip().lstrip("/*").strip()
        # Prose that happens to be indented is not an example; code has syntax.
        if any(t in body for t in ("(", ";", "=", ".", "->")):
            return True
    return False


def code_needs_air(raw):
    """True when an example is jammed against the line above it.

    An example is a change of register - prose stops, code starts - and reads as
    one when it is set off. Doxygen does not require the blank line, so nothing
    but this notices when it is missing.
    """
    prev = None
    for line in (raw or "").splitlines():
        if line.strip() == "@code" and prev is not None and prev.strip() != "":
            return True
        prev = line
    return False


def why_not_doc(raw):
    """Which of the two ways a comment can attach and still say nothing."""
    words = _words(raw)
    if not words:
        return "section divider only - hovers with decoration"
    return "only %d words - restates the name rather than describing it" % len(words)


def owner(c):
    p = c.semantic_parent
    return p.spelling if p is not None else ""

def public_api(tu, path):
    want = path.replace('\\\\', "/").lower()
    for c in tu.cursor.walk_preorder():
        if c.kind not in KINDS:
            continue
        if not c.location.file or c.location.file.name.replace('\\\\', "/").lower() != want:
            continue
        if c.access_specifier not in (ci.AccessSpecifier.PUBLIC, ci.AccessSpecifier.INVALID):
            continue
        if c.spelling.startswith("_"):
            continue
        if c.kind == ci.CursorKind.FIELD_DECL and not owner(c).startswith(FIELD_OWNER):
            continue
        yield c

def show(tu, path, needle):
    """Print the comment an editor would attach to each matching declaration.

    raw_comment is the same attachment clangd uses, so this is what hover shows -
    no need to check by hovering.
    """
    found = 0
    for c in public_api(tu, path):
        if needle.lower() not in c.spelling.lower():
            continue
        found += 1
        if c.kind == ci.CursorKind.FIELD_DECL:
            what = "%s %s" % (c.type.spelling, c.spelling)
        else:
            what = "%s(%s)" % (c.spelling,
                               ", ".join(a.type.spelling for a in c.get_arguments()))
        print("%s   [line %d]" % (what, c.location.line))
        raw = c.raw_comment
        if not raw:
            print("    <nothing - hovers blank>")
        elif not is_doc(raw):
            print("    <%s>" % why_not_doc(raw))
        for line in (raw or "").splitlines():
            print("    | " + line.strip())
        print()
    if not found:
        print("no public declaration matching %r" % needle)


def example_lines(raw):
    """The code a @code block is made of, and its offset within the comment.

    The offset is what lets the generated sketch carry #line directives back to
    the header, so a compiler error names src/BlaeckSerial.h and not a generated
    file the contributor does not have.

    Doxygen delimits it, so that is read exactly. The plain-comment style is
    inferred from the indent, and stops at the first prose line after the block -
    a comment may explain something below its example, and that prose is not code.
    """
    lines, inside, seen_any, first = [], False, False, None
    for i, line in enumerate(raw.splitlines()):
        bare = line.strip().lstrip("/*").strip()
        if bare.startswith("@code"):
            inside, seen_any = True, True
            continue
        if bare.startswith("@endcode"):
            inside = False
            continue
        if inside:
            if first is None:
                first = i
            lines.append(line.strip().lstrip("/*").rstrip())
        elif not seen_any:
            if EXAMPLE.match(line):
                if first is None:
                    first = i
                lines.append(line.strip().lstrip("/*").rstrip())
            elif lines:
                break
    if not seen_any and not any(l.rstrip().endswith((";", "{", "}")) for l in lines):
        # An indented block with no statement in it is a table, not code - the
        # interval_ms table on setIntervalMs reads exactly like an example otherwise.
        return [], None
    return lines, first


# A line that opens a definition rather than performing a step: it has to go at file
# scope, because C++ has no nested functions and a handler is the natural example for
# a command callback.
DEFINITION = re.compile(r"^(void|bool|byte|int|long|float|double|unsigned|char)\b.*\)\s*$")


def line_directive(n, filename):
    """Point the compiler at the real source of the lines that follow.

    Without this a build error names the generated sketch, which is gitignored -
    so the one file a contributor would need in order to find the failing block is
    the one file they do not have.
    """
    return '#line %d "%s"' % (n, filename.replace(chr(92), "/"))


def extract(tu, path, dest):
    """Every @code block in the header, as one sketch a compiler can check.

    A block that does not compile is worse than none: it is read as authoritative
    and followed. Rust checks its doc examples by building them, which is what lets
    it require one everywhere; this is the same trick for a header. Each block
    becomes a function body, so it has to be complete statements rather than a
    fragment of a chain - which is a constraint on how they are written, and the
    right one.
    """
    seen = set()
    out = ['// GENERATED by extras/scripts/checkdocs.py --extract.',
           '// Do not edit: the next extraction overwrites it. Do not commit: it is gitignored.',
           '//',
           '// Every @code block from src/BlaeckSerial.h - the snippets an editor shows when',
           '// someone hovers a method - each given a function of its own so that a compiler',
           '// reads it. A block calling a method that no longer exists fails this build,',
           '// rather than being shown on hover as instructions that do not work.',
           '//',
           '// The globals these blocks use without declaring them live in preamble.h.',
           '#include "preamble.h"',
           '']
    n = 0
    for c in public_api(tu, path):
        raw = c.raw_comment
        if not raw or not has_example(raw):
            continue
        key = (owner(c), c.spelling)
        if key in seen:
            continue
        seen.add(key)
        lines, offset = example_lines(raw)
        if not lines:
            continue
        n += 1
        where = "%s::%s" % key if key[0] else key[1]
        # The comment sits immediately above the declaration - rule 1 requires it, and
        # a blank line between them would detach it - so its first line is the
        # declaration's line less the comment's height. The block's own lines are
        # contiguous within it, which makes one #line per block exact for all of them.
        first = c.location.line - len(raw.splitlines()) + offset
        out.append("// %s  (%s:%d)" % (where, path.replace(chr(92), "/"), first))
        if DEFINITION.match(lines[0].strip()):
            # "void loop()" is the clearest way for an example to say where a call
            # belongs, and several examples rightly show it. They collide here, so
            # they are renamed on the way in - the header keeps what reads best.
            head = lines[0].rstrip()
            for name in ("loop", "setup"):
                if head.strip() == "void %s()" % name:
                    head = "void _doc%s%d()" % (name.capitalize(), n)
            out.append(head)
            out.append(line_directive(first + 1, path))
            out.extend(l.rstrip() for l in lines[1:])
        else:
            out.append("void _docExample%d()" % n)
            out.append("{")
            out.append(line_directive(first, path))
            out.extend(l.rstrip() for l in lines)
            out.append("}")
        # Hand the numbering back to the generated file, or every line after this
        # block is reported against the header too.
        out.append(line_directive(len(out) + 2, dest))
        out.append("")
    out += ['void setup() {}', 'void loop() {}', '']
    return NL.join(out), n


def main(argv):
    path = argv[0]
    extra = argv[argv.index("--") + 1:] if "--" in argv else []
    args = ["-x", "c++", "-std=c++11", "-fparse-all-comments", "-ferror-limit=0"] + extra
    tu = ci.Index.create().parse(path, args=args,
                                 options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)

    if "--extract" in argv:
        dest = argv[argv.index("--extract") + 1]
        text, n = extract(tu, path, dest)
        io.open(dest, "w", encoding="utf-8", newline=NL).write(text)
        print("%d example(s) -> %s" % (n, dest))
        return 0

    if "--show" in argv:
        show(tu, path, argv[argv.index("--show") + 1])
        return 0

    groups = collections.OrderedDict()
    for c in public_api(tu, path):
        groups.setdefault((owner(c), c.spelling), []).append(c)

    bare, partial, noexample, jammed = [], [], [], []
    for key, members in groups.items():
        documented = [m for m in members if is_doc(m.raw_comment)]
        if not documented:
            bare.append((key, members))
        elif len(documented) < len(members):
            partial.append((key, members, len(documented)))
        if documented and not any(has_example(m.raw_comment) for m in members):
            noexample.append((key, members))
        if any(code_needs_air(m.raw_comment) for m in members):
            jammed.append((key, members))

    print("%s: %d public names, %d with no comment on any overload"
          % (path, len(groups), len(bare)))
    if bare:
        print()
        for (cls, name), members in bare:
            where = "%s::%s" % (cls, name) if cls else name
            print("  %5d  %-44s %d overload(s)" % (members[0].location.line, where, len(members)))
    if partial:
        print()
        print("  (%d name(s) documented on some overloads only - normal for a type-overload set)"
              % len(partial))

    if jammed:
        print()
        print("%d name(s) with an example jammed against the line above it" % len(jammed))
        for (cls, name), members in jammed:
            print("  %5d  %s" % (members[0].location.line,
                                 "%s::%s" % (cls, name) if cls else name))

    print()
    print("%d documented name(s) with no example" % len(noexample))
    if noexample and "-v" in argv:
        for (cls, name), members in noexample:
            where = "%s::%s" % (cls, name) if cls else name
            print("  %5d  %s" % (members[0].location.line, where))
    return 1 if bare else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
