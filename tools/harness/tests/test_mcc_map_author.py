"""A Workshop map's author in the map lists' panel (mcc_menu_text.c): two lines under the description, which is cut short above them."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, structure, read, run

# a fault in mcc_menu_text.c and the case that must catch it
NEGATIVE_CONTROLS = {
    'room-too-small': (('height-=text_height(author)-text_height(L"x");', 'height-=text_height(author+2);'), 'long'),
    'no-room': (('height-=text_height(author)-text_height(L"x");', ';'), 'long'),
    'dots-overwritten': (('        for (used=0; wrapped[used]; used++) {}\n', ''), 'long'),
    'name-uncut': (('while (width_of(text+start)>width && used>start)', 'while (width_of(text+start)>SHORT_MAX && used>start)'),
                   'long-name'),
    'any-field': (('    if (strcmp(leaf,"mp_map_right_data")) return 0;\n', ''), 'other-field'),
    'broken-characters-kept': (('if (following || first<0xC0 || c>0xFFFF) c=\'?\';', ';'), 'characters'),
}

CASES = ['short', 'long', 'no-author', 'other-field', 'long-name', 'no-description', 'characters']


def generated(fault=None):
    text = read('port/linux/game/mcc_menu_text.c')
    maps = read('port/linux/game/mcc_maps.h')
    authors = read('port/linux/src/workshop_authors.h')
    constants = ''.join(f'#define {name} {constant(source, name)}\n' for source, name in (
        (maps, 'MCC_MAPS_FIRST_DISPLAY_INDEX'), (maps, 'MCC_MAPS_MAXIMUM'), (authors, 'WORKSHOP_AUTHORS_NAME_SIZE')))
    code = enum_with(text, 'AUTHOR_TEXT_SIZE') + '\n'
    # (this fork: mcc_menu_text_draw's layout cache, and author_name, author_text's check)
    code += ''.join(f'#define {name} {constant(text, name)}\n' for name in ('MCC_TEXT_LAYOUTS', 'MCC_TEXT_CACHED'))
    code += structure(text, 'mcc_text_layout') + ';\n'
    code += 'static struct mcc_text_layout *mcc_text_layouts;\nstatic unsigned mcc_text_stamp;\n'
    code += ''.join(function(text, name) + '\n' for name in ('field', 'width_of', 'wrap', 'text_height', 'mcc_text_same',
                                                             'mcc_text_length', 'author_name', 'author_text',
                                                             'mcc_menu_text_draw'))
    if fault:
        code = mutated(code, *fault)
    return (('constants.inc', constants), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_mcc_map_author(case):
    status, output = run(build('mcc_map_author', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('control', NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build('mcc_map_author', generated(fault)), case)
    assert status == CHECK_FAILED, f'mcc_menu_text.c with a fault ({control}) passed {case!r}: the test cannot see it'
