#!/usr/bin/env python3
"""Assemble the manuscript and render its small Markdown subset, without packages."""
from pathlib import Path
import html
import re

ROOT = Path(__file__).resolve().parents[1]


def expand(text):
    def include(match):
        relative = match.group(1)
        path = (ROOT / relative).resolve()
        if ROOT not in path.parents or path.suffix not in (".c", ".h"):
            raise ValueError("unsupported source include: " + relative)
        return "```c\n" + path.read_text().rstrip() + "\n```"
    result = re.sub(r"^\{\{include:([^}]+)\}\}$", include, text, flags=re.M)
    if "{{include:" in result:
        raise ValueError("unexpanded source include")
    return result


def inline(text):
    saved = []

    def save(value):
        saved.append(value)
        return "\x00" + str(len(saved) - 1) + "\x00"

    text = re.sub(r"`([^`]+)`", lambda m: save("<code>" + html.escape(m[1]) + "</code>"), text)
    text = re.sub(r"\[([^\]]+)\]\(([^\s)]+)\)",
                  lambda m: save('<a href="' + html.escape(m[2], quote=True) + '">' +
                                 inline(m[1]) + "</a>"), text)
    text = html.escape(text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)
    text = text.replace("  \n", "<br>\n").replace("\n", " ")
    for index, value in reversed(list(enumerate(saved))):
        text = text.replace("\x00" + str(index) + "\x00", value)
    return text


def slug(text):
    # Match the common GitHub-style heading anchors used by Markdown readers.
    return re.sub(r"[^\w\s-]", "", text.lower()).replace(" ", "-")


def readable_markdown(manuscript):
    """Export disclosure sections as ordinary Markdown-compatible HTML."""
    lines, depth, fenced = [], 0, False
    for line in manuscript.splitlines():
        if line.startswith("```"):
            fenced = not fenced
        if not fenced and line.startswith(":::reveal "):
            if depth:
                raise ValueError("nested reveal sections are unsupported")
            depth = 1
            lines.extend(["<details>", "<summary>" +
                          html.escape(line[len(":::reveal "):]) + "</summary>", ""])
        elif not fenced and line.strip() == ":::":
            if not depth:
                raise ValueError("unmatched reveal end")
            depth = 0
            lines.extend(["", "</details>"])
        else:
            lines.append(line)
    if depth:
        raise ValueError("unclosed reveal section")
    return "\n".join(lines) + "\n"


def render(markdown):
    lines = markdown.splitlines()
    output, navigation, used = [], [], {}
    i, reveal_depth = 0, 0

    def is_block(line):
        return bool(re.match(r"^(#{1,6} |```|:::|[-*] |\d+\. |> |\|)", line))

    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        if line.startswith(":::reveal "):
            if reveal_depth:
                raise ValueError("nested reveal sections are unsupported")
            reveal_depth = 1
            output.append('<details class="reveal"><summary>' +
                          inline(line[len(":::reveal "):]) +
                          '</summary><div class="reveal-body">')
            i += 1
            continue
        if line.strip() == ":::":
            if not reveal_depth:
                raise ValueError("unmatched reveal end")
            reveal_depth = 0
            output.append("</div></details>")
            i += 1
            continue
        if line.startswith("```"):
            language = line[3:].strip()
            code = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            if i == len(lines):
                raise ValueError("unclosed code fence")
            output.append('<pre class="language-' + html.escape(language, quote=True) +
                          '"><code>' + html.escape("\n".join(code)) + "</code></pre>")
            i += 1
            continue
        heading = re.match(r"^(#{1,6}) (.+)$", line)
        if heading:
            level, title = len(heading[1]), heading[2]
            base = slug(title)
            used[base] = used.get(base, 0) + 1
            identifier = base if used[base] == 1 else base + "-" + str(used[base])
            output.append(f'<h{level} id="{identifier}">{inline(title)}</h{level}>')
            if level == 1:
                navigation.append((identifier, title))
            i += 1
            continue
        if (line.startswith("|") and i + 1 < len(lines)
                and re.fullmatch(r"[\s|:-]+", lines[i + 1])):
            headers = line.strip().strip("|").split("|")
            output.append("<div class=table-scroll><table><thead><tr>" +
                          "".join("<th>" + inline(x.strip()) + "</th>" for x in headers) +
                          "</tr></thead><tbody>")
            i += 2
            while i < len(lines) and lines[i].startswith("|"):
                cells = lines[i].strip().strip("|").split("|")
                output.append("<tr>" + "".join("<td>" + inline(x.strip()) + "</td>" for x in cells) + "</tr>")
                i += 1
            output.append("</tbody></table></div>")
            continue
        item = re.match(r"^([-*]|\d+\.) (.*)$", line)
        if item:
            ordered = item[1][0].isdigit()
            tag = "ol" if ordered else "ul"
            output.append("<" + tag + ">")
            while i < len(lines):
                current = re.match(r"^([-*]|\d+\.) (.*)$", lines[i])
                if not current or current[1][0].isdigit() != ordered:
                    break
                parts = [current[2]]
                i += 1
                while i < len(lines) and lines[i].startswith("   ") and not lines[i].startswith("```"):
                    parts.append(lines[i].strip())
                    i += 1
                output.append("<li>" + inline("\n".join(parts)) + "</li>")
                if i + 1 < len(lines) and not lines[i].strip() and re.match(r"^([-*]|\d+\.) ", lines[i + 1]):
                    i += 1
            output.append("</" + tag + ">")
            continue
        if line.startswith("> "):
            quote = []
            while i < len(lines) and lines[i].startswith("> "):
                quote.append(lines[i][2:])
                i += 1
            output.append("<blockquote><p>" + inline("\n".join(quote)) + "</p></blockquote>")
            continue
        if re.fullmatch(r"---+", line):
            output.append("<hr>")
            i += 1
            continue
        paragraph = [line]
        i += 1
        while i < len(lines) and lines[i].strip() and not is_block(lines[i]):
            paragraph.append(lines[i])
            i += 1
        output.append("<p>" + inline("\n".join(paragraph)) + "</p>")
    if reveal_depth:
        raise ValueError("unclosed reveal section")
    return "\n".join(output), navigation


CSS = """
:root { color-scheme: light; --ink:#20303d; --accent:#176b71; --muted:#546571; }
* { box-sizing:border-box; }
html { scroll-behavior:smooth; }
body { margin:0; background:#faf9f6; color:var(--ink); font:18px/1.7 Georgia,serif; }
a { color:var(--accent); text-underline-offset:.16em; }
nav { position:fixed; inset:0 auto 0 0; width:280px; padding:30px 24px;
      overflow:auto; background:#eef2f1; border-right:1px solid #d3dddb;
      font:14px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; }
nav strong { display:block; font-size:19px; margin-bottom:8px; }
nav .edition { margin:0 0 22px; color:var(--muted); }
nav a { display:block; padding:7px 0; text-decoration:none; }
nav a:hover { text-decoration:underline; }
main { max-width:1080px; margin:0 auto 0 280px; padding:52px 64px 110px; }
h1,h2,h3,h4 { font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
             line-height:1.22; color:#143f49; scroll-margin-top:24px; }
h1 { margin:3em 0 .85em; padding-top:1em; border-top:2px solid #c1d2d1; font-size:2.25em; }
h1:first-child { margin-top:0; padding-top:0; border:0; font-size:3.2em; letter-spacing:-.035em; }
h2 { font-size:1.4em; margin:2em 0 .8em; } h3 { font-size:1.15em; margin:1.8em 0 .7em; }
p { margin:.8em 0 1.15em; } li { margin:.45em 0; } strong { font-weight:700; }
code { font: .83em/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
       background:#eaf0ee; padding:.13em .3em; border-radius:3px; }
pre { overflow:auto; padding:20px 22px; margin:1.4em 0; background:#eaf0ee;
      border:1px solid #d5e0db; border-radius:6px; line-height:1.45; tab-size:4; }
pre code { background:none; padding:0; font-size:13px; line-height:1.5; }
table { border-collapse:collapse; width:100%; font-size:.88em; margin:1.4em 0; }
th,td { padding:10px 13px; border:1px solid #d5dedb; vertical-align:top; text-align:left; }
th { background:#e8efeb; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; }
.table-scroll { overflow-x:auto; } blockquote { border-left:4px solid #4b9090; padding-left:22px; margin-left:0; }
.top-link { font:14px -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; }
.reading-controls { margin:18px 0 22px; display:grid; gap:8px; }
.reading-controls[hidden] { display:none; }
.reading-controls button { padding:9px 10px; border:1px solid #8baaa7; border-radius:4px;
 background:#fff; color:#17474d; text-align:left; font:inherit; cursor:pointer; }
.reading-controls button:hover { background:#dfece8; }
.reading-controls button:focus-visible, summary:focus-visible { outline:3px solid #267f8a; outline-offset:3px; }
.reveal { margin:1.5em 0; border:1px solid #b6ccc7; border-radius:6px; background:#f4f7f3; }
.reveal summary { cursor:pointer; padding:14px 18px; font:600 .9em/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; color:#164e55; }
.reveal[open] summary { border-bottom:1px solid #d4e0db; }
.reveal-body { padding:4px 20px 12px; }
@media(min-width:1450px) { main { margin-left:calc(280px + (100vw - 1450px)/2); } }
@media(max-width:900px) { nav { position:relative; width:auto; max-height:340px; border-right:0;
                               border-bottom:1px solid #d3dddb; } main { margin:0 auto; padding:32px 24px 70px; }
                         h1 { font-size:1.9em; } h1:first-child { font-size:2.7em; } }
@media print { @page { size:A4; margin:20mm 18mm; } nav { display:none; }
 body { background:white; color:#111; font-size:10.5pt; line-height:1.5; }
 main { padding:0; margin:0; max-width:none; } h1 { break-before:page; border:0; padding-top:0; margin-top:0; }
 h1:first-child { break-before:auto; margin-top:20mm; } h2,h3,h4 { break-after:avoid; }
 pre { overflow:visible; white-space:pre-wrap; overflow-wrap:anywhere; border:1px solid #ddd; padding:10px; }
 pre code { font-size:7.5pt; } tr { break-inside:avoid; } a { color:inherit; } p { orphans:3; widows:3; }
 .top-link { display:none; }
 .reveal { background:white; border:0; } .reveal summary { padding:8px 0; }
 .reveal-body { padding:0; } details::details-content { content-visibility:visible; }
}
"""


READER_SCRIPT = """
<script>
(() => {
  const sections = Array.from(document.querySelectorAll('details.reveal'));
  const controls = document.querySelector('.reading-controls');
  controls.hidden = false;
  document.getElementById('expand-references').addEventListener('click', () => {
    sections.forEach(section => { section.open = true; });
  });
  document.getElementById('collapse-references').addEventListener('click', () => {
    sections.forEach(section => { section.open = false; });
  });
  let printState = null;
  window.addEventListener('beforeprint', () => {
    if (printState === null) printState = sections.map(section => section.open);
    sections.forEach(section => { section.open = true; });
  });
  window.addEventListener('afterprint', () => {
    if (printState !== null) sections.forEach((section, i) => { section.open = printState[i]; });
    printState = null;
  });
  function exposeTarget() {
    if (!window.location.hash) return;
    let identifier;
    try { identifier = decodeURIComponent(window.location.hash.slice(1)); }
    catch (_) { return; }
    const target = document.getElementById(identifier);
    if (!target) return;
    for (let parent = target.parentElement; parent; parent = parent.parentElement) {
      if (parent.tagName === 'DETAILS') parent.open = true;
    }
    target.scrollIntoView();
  }
  window.addEventListener('hashchange', exposeTarget);
  exposeTarget();
})();
</script>
"""


def main():
    chapters = sorted((ROOT / "chapters").glob("*.md"))
    if len([p for p in chapters if re.match(r"(0[1-9]|1[0-3])-", p.name)]) != 13:
        raise ValueError("all thirteen chapters are required")
    if not (ROOT / "chapters/90-appendices.md").is_file():
        raise ValueError("appendices are required")
    if not (ROOT / "chapters/00a-reasoning.md").is_file():
        raise ValueError("the reasoning method is required")
    pieces = [expand(p.read_text()) for p in chapters]
    titles = re.findall(r"^# (.+)$", "\n".join(pieces[1:]), flags=re.M)
    contents = "# Contents\n\n" + "\n".join("- [" + title + "](#" + slug(title) + ")" for title in titles)
    manuscript = pieces[0].rstrip() + "\n\n" + contents + "\n\n" + "\n\n".join(pieces[1:])
    markdown_edition = readable_markdown(manuscript)
    body, entries = render(manuscript)
    navigation = "\n".join('<a href="#' + identifier + '">' + inline(title) + '</a>'
                           for identifier, title in entries)
    document = ("<!doctype html>\n<html lang=en><head><meta charset=utf-8>"
                '<meta name="viewport" content="width=device-width,initial-scale=1">'
                '<meta name="description" content="Derive systems designs from requirements and contracts, challenge them with experiments, and build working C programs for macOS and Linux.">'
                "<title>At the Boundary — Systems Programming in C</title><style>" + CSS +
                "</style></head><body><nav aria-label=\"Book contents\"><strong>At the Boundary</strong>"
                '<p class="edition">Systems Programming in C<br>First-principles study edition · October 2026</p>' +
                '<div class="reading-controls" hidden><button type="button" id="expand-references">Expand code and answers</button>'
                '<button type="button" id="collapse-references">Fold code and answers</button></div>' +
                navigation + "</nav><main>" + body +
                '<p class="top-link"><a href="#at-the-boundary">Return to the beginning</a></p>' +
                "</main>" + READER_SCRIPT + "</body></html>\n")
    (ROOT / "BOOK.md").write_text(markdown_edition)
    (ROOT / "BOOK.html").write_text(document)
    prose = re.sub(r"```.*?```", "", manuscript, flags=re.S)
    print(f"Built BOOK.md and BOOK.html: {len(prose.split()):,} prose words, {len(entries)} major sections.")


if __name__ == "__main__":
    main()
