"""Add the missing settings to lvonasek's options page.

Every setting the game normally has, plus anything anyone has
added, plus anything we can add - "it's a modern system, so people should be
able to tweak settings that didn't exist before."

What this adds, and why not more
--------------------------------
lvonasek rewrote Prey's options page and dropped 31 of its settings. 14 of those
are declared in this engine and read by nothing - com_profanity,
g_levelloadmusic, r_correctspecular, r_lowParticleDetail, r_normalizebumpmap,
r_shaderlevel, r_skipGlowOverlay, r_skipNewAmbient, r_swapInterval,
r_useFastSkinning, s_deviceName, s_musicvolume_dB, s_reverse and ui_showGun.
A control for those would move a number nothing reads, which is worse than no
control: it looks like it works. They are left out deliberately and the README
says so.

Three more are live but meaningless in a headset - r_fullscreen, r_mode and
r_multisamples are window resolution and MSAA. vr_supersampling is the real
resolution lever and MSAA does not exist on this render path at all, so putting
them on a VR menu would be three more controls that appear to do something.

That leaves the ones below: settings the game reads, on the tab they belong to.

The rows are generated rather than hand-written because the format is
repetitive and a typo is cheap to make. Generating them means one template,
copied from a row known to work.

How much the engine will tell you, measured rather than assumed: "reloadGuis
all" in the console DOES report structural damage - one closing brace removed
from this file produced "line 855: Unexpected end of file". What it cannot
report is a row that parses and is still wrong: a misspelt cvar, a material that
does not exist, a rect off the bottom of the page. Those are what
tools/release/audit_menu_cvars.py and audit_menu_assets.py are for.

    python tools/headless/console.py "reloadGuis all"

    python tools/release/add_menu_options.py [--check]
"""
import argparse
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GUI = os.path.join(REPO, 'app', 'src', 'main', 'pk4', 'guis',
                   'mainmenu', 'mainmenu_options.guifragment')

ROW_H = 30
T = '\t' * 7          # the indent rows sit at inside a Page


def slider(prefix, label, cvar, y, low, high, step):
    return f'''
{T}buttonDef {prefix}_Row {{
{T}\trect\t\t\t10, {y}, 400, 25
{T}\tleftMat\t\t\t"guis/assets/menu/textbar_corner.tga"
{T}\tmiddleMat\t\t"guis/assets/menu/textbarstrip.tga"
{T}\trightMat\t\t"guis/assets/menu/textbar_invcorner.tga"

{T}\twindowDef {prefix}_Title {{
{T}\t\trect\t\t0, 0, 200, 25
{T}\t\ttext\t\t"{label}"
{T}\t\ttextalign\t1
{T}\t\ttextaligny\t0
{T}\t\ttextscale\t0.33
{T}\t\tshadow\t\t1
{T}\t\tforecolor\tSTATIC_FORECOLOR_COMMA
{T}\t}}
{T}\twindowDef {prefix}_BG {{
{T}\t\trect\t\t250, 4, 100, 16
{T}\t\tbackground\t"guis/assets/sliderbackground.tga"
{T}\t}}
{T}\tsliderDef {prefix}_Slider {{
{T}\t\trect\t\t250, 4, 100, 16
{T}\t\tforecolor\tEDITABLE_FORECOLOR_COMMA
{T}\t\thovercolor\tEDITABLE_HOVERCOLOR_COMMA
{T}\t\tlow\t\t\t{low}
{T}\t\thigh\t\t{high}
{T}\t\tstep\t\t{step}
{T}\t\tthumbShader\t"guis/assets/sliderbutton.tga"
{T}\t\tcvar\t\t"{cvar}"
{T}\t\tvolumeslider\t1
{T}\t}}
{T}}}
'''


def choice(prefix, label, cvar, y, choices, values):
    return f'''
{T}buttonDef {prefix}_Row {{
{T}\trect\t\t\t10, {y}, 400, 25
{T}\tleftMat\t\t\t"guis/assets/menu/textbar_corner.tga"
{T}\tmiddleMat\t\t"guis/assets/menu/textbarstrip.tga"
{T}\trightMat\t\t"guis/assets/menu/textbar_invcorner.tga"
{T}\tvisible\t\t\t1

{T}\twindowDef {prefix}_Title {{
{T}\t\trect\t\t0, 0, 200, 25
{T}\t\tforecolor\tSTATIC_FORECOLOR_COMMA
{T}\t\ttextalign\t1
{T}\t\ttextaligny\t0
{T}\t\ttextscale\t0.33
{T}\t\tshadow\t\t1
{T}\t\ttext\t\t"{label}"
{T}\t}}
{T}\tchoiceDef {prefix}_Choice {{
{T}\t\trect\t\t200, 0, 200, 25
{T}\t\tforecolor\tEDITABLE_FORECOLOR_COMMA
{T}\t\thovercolor\tEDITABLE_HOVERCOLOR_COMMA
{T}\t\ttextalign\t1
{T}\t\ttextaligny\t0
{T}\t\ttextscale\t0.33
{T}\t\tshadow\t\t1
{T}\t\tchoices\t\t"{choices}"
{T}\t\tvalues\t\t"{values}"
{T}\t\tcvar\t\t"{cvar}"
{T}\t\tchoiceType\t1
{T}\t}}
{T}}}
'''


# tab -> rows to append. Order is the order they appear.
ADDITIONS = {
    'Options_VR': [
        ('choice', 'PCVRO_Portal', 'Portal draw distance', 'pcvr_portalDistance',
         'As the map intended;Double;Triple;Any range', '1;2;3;0'),
        ('choice', 'PCVRO_Tics', 'Lock world to frame rate', 'pcvr_frameLockedTics',
         'Off;On', '0;1'),
        ('choice', 'PCVRO_Mirror', 'Desktop mirror', 'pcvr_mirror',
         'Off;Left eye;Right eye', '0;1;2'),
        ('slider', 'PCVRO_TurnSpeed', 'Smooth turn speed', 'pcvr_smoothTurnSpeed',
         '45', '250', '5'),
        ('slider', 'PCVRO_BloomScale', 'Bloom strength', 'pcvr_bloomScale',
         '0', '1.0', '0.05'),
        ('choice', 'PCVRO_BloomRange', 'Bloom quality', 'pcvr_bloomRange',
         'One quad;3x3;5x5;7x7 (full)', '0;1;2;3'),
        ('choice', 'PCVRO_Aniso', 'Texture filtering', 'image_anisotropy',
         'Off;2x;4x;8x;16x', '1;2;4;8;16'),
    ],
    # These three were on Options_Video and there is no room there: its ten
    # rows already fill every slot from 40 to 310 on a 350-tall page. They are
    # PCVR settings on a VR port, and the VR tab has exactly three slots free.
    'Options_Video': [],
    'Options_GameOptions': [
        ('slider', 'PCVRO_Volume', 'Master volume', 's_volume_dB',
         '-40', '0', '1'),
        ('choice', 'PCVRO_Crosshair', 'Crosshair', 'g_crosshair', 'Off;On', '0;1'),
        ('choice', 'PCVRO_DDA', 'Adaptive difficulty', 'g_useDDA', 'Off;On', '0;1'),
    ],
}


def tab_span(text, name):
    m = re.search(r'tabDef\s+' + name + r'\s*\{', text)
    if not m:
        sys.exit('no tab %s' % name)
    nxt = re.search(r'tabDef\s+Options_\w+\s*\{', text[m.end():])
    return m.start(), (m.end() + nxt.start()) if nxt else len(text)


def content_bottom(body):
    """The lowest pixel any existing row on this page draws.

    Not the lowest row's y: a row carrying a warning label draws 45 down from
    its own top, and appending on the next 30 slot put a new row through it.
    """
    low = 0
    for r in re.finditer(r'buttonDef\s+\w+\s*\{\s*rect\s+10,\s*(\d+),', body):
        y = int(r.group(1))
        depth, end = 0, len(body)
        for i in range(body.index('{', r.start()), len(body)):
            if body[i] == '{':
                depth += 1
            elif body[i] == '}':
                depth -= 1
                if depth == 0:
                    end = i
                    break
        inner = body[r.start():end]
        reach = 25
        first = True   # the first rect inside the braces is the row's own
        for m in re.finditer(
                r'rect\s+[-\d.]+,\s*([-\d.]+),\s*[-\d.]+,\s*([-\d.]+)',
                inner[inner.index('{') + 1:]):
            if first:
                first = False
                continue
            reach = max(reach, int(float(m.group(1))) + int(float(m.group(2))))
        low = max(low, y + reach)
    return low


def last_row_y(body):
    """The y of the lowest row on the page.

    Matched on the x of 10, because that is what every row of every page shares.
    Matching the full rect missed the 480x50 rows that carry a warning label,
    returned a y three slots too high, and stacked new rows on top of them.
    """
    ys = [int(y) for y in re.findall(
        r'buttonDef\s+\w+\s*\{\s*rect\s+10,\s*(\d+),', body)]
    return max(ys) if ys else 10


def page_bottom(body):
    """Where the page the rows live in stops, in the same coordinates."""
    m = re.search(r'windowDef\s+\w*Page\s*\{\s*rect\s+[-\d]+,\s*([-\d]+),'
                  r'\s*[-\d]+,\s*([-\d]+)', body)
    return int(m.group(2)) if m else 350


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='report, change nothing')
    args = ap.parse_args()

    text = io.open(GUI, encoding='latin-1', newline='').read()
    added = 0

    for tab, rows in ADDITIONS.items():
        a, b = tab_span(text, tab)
        body = text[a:b]
        if 'PCVRO_' in body:
            print('%-22s already has the new rows - skipping' % tab)
            continue
        # Start on the first grid slot clear of everything already drawn, so a
        # row whose warning label hangs below it is stepped over rather than
        # written through.
        y = last_row_y(body)
        low = content_bottom(body)
        while y + ROW_H < low:
            y += ROW_H

        # The rows are siblings of the existing ones, so they go just before the
        # closing brace of the Page windowDef that holds them. Counting closing
        # braces backwards from the end of the tab put them one level too deep -
        # inside the last row - and the only thing that caught it was comparing
        # brace depth against a row known to be right.
        pm = re.search(r'windowDef\s+\w*Page\s*\{', body)
        if not pm:
            sys.exit('no Page windowDef in %s' % tab)
        depth = 0
        end = None
        for i in range(pm.end() - 1, len(body)):
            if body[i] == '{':
                depth += 1
            elif body[i] == '}':
                depth -= 1
                if depth == 0:
                    end = i
                    break
        if end is None:
            sys.exit('unbalanced Page windowDef in %s' % tab)
        bottom = page_bottom(body)
        block = ''
        for row in rows:
            y += ROW_H
            if y + 25 > bottom:
                sys.exit(
                    '%s: row %s would sit at y=%d, past the bottom of a %d-tall '
                    'page. Move it to a tab with room rather than off the '
                    'screen.' % (tab, row[1], y, bottom))
            if row[0] == 'slider':
                _, pre, label, cvar, low, high, step = row
                block += slider(pre, label, cvar, y, low, high, step)
            else:
                _, pre, label, cvar, ch, va = row
                block += choice(pre, label, cvar, y, ch, va)
            added += 1
        newbody = body[:end] + block + body[end:]
        text = text[:a] + newbody + text[b:]
        print('%-22s +%d rows, last at y=%d' % (tab, len(rows), y))

    if args.check:
        print('\n--check: nothing written')
        return
    io.open(GUI, 'w', encoding='latin-1', newline='').write(text)
    print('\n%d rows added to %s' % (added, os.path.relpath(GUI, REPO)))


if __name__ == '__main__':
    main()
