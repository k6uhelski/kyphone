"""
test_state_machine.py — Unit tests for KyPhone OS 0.1 state machine.

Runs without hardware (SPI/GPIO) or a display. All SPI sends are mocked.

    python3 -m pytest spi_bridge/tests/test_state_machine.py -v
"""

import os
import sys
import threading
import unittest
from unittest.mock import MagicMock, patch

# ── Mock hardware modules before importing kyphone_os ─────────────────────────
sys.argv = ['test', '--sim']  # force SIM_MODE=True so hardware imports are skipped
sys.modules.setdefault('spidev', MagicMock())
sys.modules.setdefault('gpiod', MagicMock())
sys.modules.setdefault('input_handler', MagicMock())
sys.modules.setdefault('pygame', MagicMock())
sys.modules.setdefault('simulator', MagicMock())
sys.modules.setdefault('evdev', MagicMock())
_twilio_mock = MagicMock()
sys.modules.setdefault('twilio', _twilio_mock)
sys.modules.setdefault('twilio.rest', _twilio_mock)

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import kyphone_os  # noqa: E402  (import after sys.path manipulation)

# ─────────────────────────────────────────────────────────────────────────────


def reset_state(**overrides):
    """Reset kyphone_os.state to a clean baseline (does not touch the Lock)."""
    defaults = {
        'screen': 'lock',
        'home_index': 0,
        'texts_index': 0,
        'texts_header_sel': 'back',
        'thread_id': None,
        'thread_draft': '',
        'compose_to': '',
        'compose_msg': '',
        'compose_to_active': True,
        'quote_index': 0,
        'messages': [],
        'last_sid': None,
        'running': True,
    }
    defaults.update(overrides)
    for k, v in defaults.items():
        kyphone_os.state[k] = v


# ═══════════════════════════════════════════════════════════════════════════════
# Lock Screen
# ═══════════════════════════════════════════════════════════════════════════════

class TestLockScreen(unittest.TestCase):
    def setUp(self):
        reset_state(screen='lock')
        self._save_patch = patch.object(kyphone_os, 'save_messages')
        self._save_patch.start()

    def tearDown(self):
        self._save_patch.stop()

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_goes_to_home(self, _ps):
        kyphone_os.handle_key('KEY_ENTER')
        self.assertEqual(kyphone_os.state['screen'], 'home')
        self.assertEqual(kyphone_os.state['home_index'], 0)

    @patch.object(kyphone_os, 'push_screen')
    def test_arrow_goes_to_home(self, _ps):
        kyphone_os.handle_key('KEY_DOWN')
        self.assertEqual(kyphone_os.state['screen'], 'home')

    @patch.object(kyphone_os, 'push_screen')
    def test_esc_goes_to_home(self, _ps):
        kyphone_os.handle_key('KEY_ESC')
        self.assertEqual(kyphone_os.state['screen'], 'home')


# ═══════════════════════════════════════════════════════════════════════════════
# Home Screen
# ═══════════════════════════════════════════════════════════════════════════════

class TestHomeScreen(unittest.TestCase):
    def setUp(self):
        reset_state(screen='home', home_index=0)
        self._save_patch = patch.object(kyphone_os, 'save_messages')
        self._save_patch.start()

    def tearDown(self):
        self._save_patch.stop()

    @patch.object(kyphone_os, 'push_screen')
    def test_down_increments_index(self, _ps):
        kyphone_os.handle_key('KEY_DOWN')
        self.assertEqual(kyphone_os.state['home_index'], 1)

    @patch.object(kyphone_os, 'push_screen')
    def test_up_decrements_index(self, _ps):
        reset_state(screen='home', home_index=2)
        kyphone_os.handle_key('KEY_UP')
        self.assertEqual(kyphone_os.state['home_index'], 1)

    @patch.object(kyphone_os, 'push_screen')
    def test_down_clamped_at_3(self, _ps):
        reset_state(screen='home', home_index=3)
        kyphone_os.handle_key('KEY_DOWN')
        self.assertEqual(kyphone_os.state['home_index'], 3)

    @patch.object(kyphone_os, 'push_screen')
    def test_up_clamped_at_0(self, _ps):
        kyphone_os.handle_key('KEY_UP')
        self.assertEqual(kyphone_os.state['home_index'], 0)

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_text_goes_to_texts_list(self, _ps):
        reset_state(screen='home', home_index=0)
        kyphone_os.handle_key('KEY_ENTER')
        self.assertEqual(kyphone_os.state['screen'], 'texts_list')
        self.assertEqual(kyphone_os.state['texts_index'], 0)

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_read_goes_to_stub(self, _ps):
        reset_state(screen='home', home_index=2)
        kyphone_os.handle_key('KEY_ENTER')
        self.assertEqual(kyphone_os.state['screen'], 'stub')

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_listen_goes_to_stub(self, _ps):
        reset_state(screen='home', home_index=3)
        kyphone_os.handle_key('KEY_ENTER')
        self.assertEqual(kyphone_os.state['screen'], 'stub')

    @patch.object(kyphone_os, 'push_screen')
    def test_esc_goes_to_lock_and_advances_quote(self, _ps):
        reset_state(screen='home', quote_index=5)
        kyphone_os.handle_key('KEY_ESC')
        self.assertEqual(kyphone_os.state['screen'], 'lock')
        self.assertEqual(kyphone_os.state['quote_index'], 6)


# ═══════════════════════════════════════════════════════════════════════════════
# Texts List Screen
# ═══════════════════════════════════════════════════════════════════════════════

# +1002 is at index 0 in messages → not the newest thread.
# +1001 sent the LAST message → newest → index 0 in the thread list.
_MSGS = [
    {'sender': '+1002', 'name': 'Bob',   'body': 'Hey',   'read': False},
    {'sender': '+1001', 'name': 'Alice', 'body': 'Hello', 'read': True},
]


class TestTextsListScreen(unittest.TestCase):
    def setUp(self):
        reset_state(screen='texts_list', texts_index=0, messages=list(_MSGS))
        self._save_patch = patch.object(kyphone_os, 'save_messages')
        self._save_patch.start()

    def tearDown(self):
        self._save_patch.stop()

    @patch.object(kyphone_os, 'push_screen')
    def test_down_increments_index(self, _ps):
        kyphone_os.handle_key('KEY_DOWN')
        self.assertEqual(kyphone_os.state['texts_index'], 1)

    @patch.object(kyphone_os, 'push_screen')
    def test_up_at_0_enters_header(self, _ps):
        kyphone_os.handle_key('KEY_UP')
        self.assertEqual(kyphone_os.state['texts_index'], -1)
        self.assertEqual(kyphone_os.state['texts_header_sel'], 'back')

    @patch.object(kyphone_os, 'push_screen')
    def test_right_in_header_goes_to_plus(self, _ps):
        reset_state(screen='texts_list', texts_index=-1, texts_header_sel='back', messages=list(_MSGS))
        kyphone_os.handle_key('KEY_RIGHT')
        self.assertEqual(kyphone_os.state['texts_header_sel'], 'plus')

    @patch.object(kyphone_os, 'push_screen')
    def test_left_in_header_goes_to_back(self, _ps):
        reset_state(screen='texts_list', texts_index=-1, texts_header_sel='plus', messages=list(_MSGS))
        kyphone_os.handle_key('KEY_LEFT')
        self.assertEqual(kyphone_os.state['texts_header_sel'], 'back')

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_on_back_header_goes_home(self, _ps):
        reset_state(screen='texts_list', texts_index=-1, texts_header_sel='back', messages=list(_MSGS))
        kyphone_os.handle_key('KEY_ENTER')
        self.assertEqual(kyphone_os.state['screen'], 'home')

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_on_plus_header_goes_compose(self, _ps):
        reset_state(screen='texts_list', texts_index=-1, texts_header_sel='plus', messages=list(_MSGS))
        kyphone_os.handle_key('KEY_ENTER')
        self.assertEqual(kyphone_os.state['screen'], 'compose')

    @patch.object(kyphone_os, 'push_screen')
    def test_plus_char_goes_compose(self, _ps):
        kyphone_os.handle_key('CHAR:+')
        self.assertEqual(kyphone_os.state['screen'], 'compose')

    @patch.object(kyphone_os, 'push_screen')
    def test_esc_goes_home(self, _ps):
        kyphone_os.handle_key('KEY_ESC')
        self.assertEqual(kyphone_os.state['screen'], 'home')

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_on_row_goes_to_thread(self, _ps):
        kyphone_os.handle_key('KEY_ENTER')
        self.assertEqual(kyphone_os.state['screen'], 'thread')
        # texts_index=0 → newest thread = +1001 (Alice, last message in _MSGS)
        self.assertEqual(kyphone_os.state['thread_id'], '+1001')

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_on_row_marks_read(self, _ps):
        reset_state(screen='texts_list', texts_index=1, messages=list(_MSGS))
        kyphone_os.handle_key('KEY_ENTER')
        # texts_index=1 → second thread = +1002 (Bob)
        bob_msgs = [m for m in kyphone_os.state['messages'] if m['sender'] == '+1002']
        self.assertTrue(all(m['read'] for m in bob_msgs))

    @patch.object(kyphone_os, 'push_screen')
    def test_down_in_header_goes_to_first_row(self, _ps):
        reset_state(screen='texts_list', texts_index=-1, texts_header_sel='back', messages=list(_MSGS))
        kyphone_os.handle_key('KEY_DOWN')
        self.assertEqual(kyphone_os.state['texts_index'], 0)


# ═══════════════════════════════════════════════════════════════════════════════
# Thread Screen
# ═══════════════════════════════════════════════════════════════════════════════

class TestThreadScreen(unittest.TestCase):
    def setUp(self):
        reset_state(screen='thread', thread_id='+1001', thread_draft='',
                    messages=[{'sender': '+1001', 'name': 'Alice', 'body': 'Hello', 'read': True}])
        self._save_patch = patch.object(kyphone_os, 'save_messages')
        self._save_patch.start()

    def tearDown(self):
        self._save_patch.stop()

    @patch.object(kyphone_os, 'push_screen')
    def test_char_appends_to_draft(self, _ps):
        kyphone_os.handle_key('CHAR:h')
        self.assertEqual(kyphone_os.state['thread_draft'], 'h')

    @patch.object(kyphone_os, 'push_screen')
    def test_multiple_chars_accumulate(self, _ps):
        kyphone_os.handle_key('CHAR:h')
        kyphone_os.handle_key('CHAR:i')
        self.assertEqual(kyphone_os.state['thread_draft'], 'hi')

    @patch.object(kyphone_os, 'push_screen')
    def test_backspace_deletes_last_char(self, _ps):
        reset_state(screen='thread', thread_id='+1001', thread_draft='hi', messages=[])
        kyphone_os.handle_key('KEY_BACKSPACE')
        self.assertEqual(kyphone_os.state['thread_draft'], 'h')

    @patch.object(kyphone_os, 'push_screen')
    def test_backspace_on_empty_draft_stays(self, _ps):
        kyphone_os.handle_key('KEY_BACKSPACE')
        self.assertEqual(kyphone_os.state['thread_draft'], '')
        self.assertEqual(kyphone_os.state['screen'], 'thread')

    @patch.object(kyphone_os, 'push_screen')
    @patch.object(kyphone_os, 'send_reply')
    def test_enter_with_draft_sends_and_clears(self, mock_send, _ps):
        reset_state(screen='thread', thread_id='+1001', thread_draft='hello', messages=[])
        kyphone_os.handle_key('KEY_ENTER')
        mock_send.assert_called_once_with('+1001', 'hello')
        self.assertEqual(kyphone_os.state['thread_draft'], '')

    @patch.object(kyphone_os, 'push_screen')
    @patch.object(kyphone_os, 'send_reply')
    def test_enter_with_empty_draft_noop(self, mock_send, _ps):
        kyphone_os.handle_key('KEY_ENTER')
        mock_send.assert_not_called()
        self.assertEqual(kyphone_os.state['screen'], 'thread')

    @patch.object(kyphone_os, 'push_screen')
    @patch.object(kyphone_os, 'send_reply')
    def test_enter_with_whitespace_draft_noop(self, mock_send, _ps):
        reset_state(screen='thread', thread_id='+1001', thread_draft='   ', messages=[])
        kyphone_os.handle_key('KEY_ENTER')
        mock_send.assert_not_called()

    @patch.object(kyphone_os, 'push_screen')
    def test_esc_goes_to_texts_list(self, _ps):
        kyphone_os.handle_key('KEY_ESC')
        self.assertEqual(kyphone_os.state['screen'], 'texts_list')


# ═══════════════════════════════════════════════════════════════════════════════
# Compose Screen
# ═══════════════════════════════════════════════════════════════════════════════

class TestComposeScreen(unittest.TestCase):
    def setUp(self):
        reset_state(screen='compose', compose_to='', compose_msg='', compose_to_active=True)
        self._save_patch = patch.object(kyphone_os, 'save_messages')
        self._save_patch.start()

    def tearDown(self):
        self._save_patch.stop()

    @patch.object(kyphone_os, 'push_screen')
    def test_char_appends_to_to_field(self, _ps):
        kyphone_os.handle_key('CHAR:a')
        self.assertEqual(kyphone_os.state['compose_to'], 'a')

    @patch.object(kyphone_os, 'push_screen')
    def test_char_appends_to_message_field_when_active(self, _ps):
        reset_state(screen='compose', compose_to='Alice', compose_msg='', compose_to_active=False)
        kyphone_os.handle_key('CHAR:h')
        self.assertEqual(kyphone_os.state['compose_msg'], 'h')

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_on_to_empty_noop(self, _ps):
        kyphone_os.handle_key('KEY_ENTER')
        self.assertTrue(kyphone_os.state['compose_to_active'])
        self.assertEqual(kyphone_os.state['screen'], 'compose')

    @patch.object(kyphone_os, 'push_screen')
    def test_enter_on_to_with_content_moves_to_message(self, _ps):
        reset_state(screen='compose', compose_to='+1999', compose_msg='', compose_to_active=True)
        kyphone_os.handle_key('KEY_ENTER')
        self.assertFalse(kyphone_os.state['compose_to_active'])
        self.assertEqual(kyphone_os.state['screen'], 'compose')

    @patch.object(kyphone_os, 'push_screen')
    @patch.object(kyphone_os, 'send_reply')
    def test_enter_on_message_with_both_creates_thread(self, mock_send, _ps):
        reset_state(screen='compose', compose_to='+1999', compose_msg='Hello!',
                    compose_to_active=False)
        kyphone_os.handle_key('KEY_ENTER')
        mock_send.assert_called_once_with('+1999', 'Hello!')
        self.assertEqual(kyphone_os.state['screen'], 'thread')
        self.assertEqual(kyphone_os.state['thread_id'], '+1999')

    @patch.object(kyphone_os, 'push_screen')
    def test_tab_toggles_active_field(self, _ps):
        kyphone_os.handle_key('KEY_TAB')
        self.assertFalse(kyphone_os.state['compose_to_active'])
        kyphone_os.handle_key('KEY_TAB')
        self.assertTrue(kyphone_os.state['compose_to_active'])

    @patch.object(kyphone_os, 'push_screen')
    def test_backspace_deletes_from_active_field(self, _ps):
        reset_state(screen='compose', compose_to='Ali', compose_msg='', compose_to_active=True)
        kyphone_os.handle_key('KEY_BACKSPACE')
        self.assertEqual(kyphone_os.state['compose_to'], 'Al')

    @patch.object(kyphone_os, 'push_screen')
    def test_esc_goes_to_texts_list(self, _ps):
        kyphone_os.handle_key('KEY_ESC')
        self.assertEqual(kyphone_os.state['screen'], 'texts_list')


if __name__ == '__main__':
    unittest.main()
