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
import collections
import re
import clang.cindex as ci

# Constructors are left out: the handle types are returned by the library, never
# constructed by a sketch, so nobody hovers them.
KINDS = {
    ci.CursorKind.CXX_METHOD,
    ci.CursorKind.FUNCTION_DECL,
    ci.CursorKind.FUNCTION_TEMPLATE,
}

SECTION = re.compile(r"^[\s/*-]*$")

def is_doc(raw):
    """True when a comment says something, rather than marking a section.

    A divider like "// ----- Tick -----" attaches to whatever follows it, so an
    editor shows it as the documentation. Stripping the punctuation leaves a
    word or two, which is not a description of anything.
    """
    if not raw:
        return False
    words = []
    for line in raw.splitlines():
        line = line.strip().lstrip("/*").strip().strip("-").strip()
        if line and not SECTION.match(line):
            words.extend(line.split())
    return len(words) >= 6


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
        args = ", ".join(a.type.spelling for a in c.get_arguments())
        print("%s(%s)   [line %d]" % (c.spelling, args, c.location.line))
        raw = c.raw_comment
        if not raw:
            print("    <nothing - hovers blank>")
        elif not is_doc(raw):
            print("    <section divider only - hovers with decoration>")
        for line in (raw or "").splitlines():
            print("    | " + line.strip())
        print()
    if not found:
        print("no public declaration matching %r" % needle)


def main(argv):
    path = argv[0]
    extra = argv[argv.index("--") + 1:] if "--" in argv else []
    args = ["-x", "c++", "-std=c++11", "-fparse-all-comments", "-ferror-limit=0"] + extra
    tu = ci.Index.create().parse(path, args=args,
                                 options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)

    if "--show" in argv:
        show(tu, path, argv[argv.index("--show") + 1])
        return 0

    groups = collections.OrderedDict()
    for c in public_api(tu, path):
        groups.setdefault((owner(c), c.spelling), []).append(c)

    bare, partial = [], []
    for key, members in groups.items():
        documented = [m for m in members if is_doc(m.raw_comment)]
        if not documented:
            bare.append((key, members))
        elif len(documented) < len(members):
            partial.append((key, members, len(documented)))

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
    return 1 if bare else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
