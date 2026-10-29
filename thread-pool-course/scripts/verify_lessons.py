"""Optional maintainer QA: pip-installed Playwright + an existing Chrome/Edge.
Does not install packages or browsers. Uses local file:// URLs in offline mode.
"""
from pathlib import Path
from html.parser import HTMLParser
import hashlib
import re
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'validation'


class Structure(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.links = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.append(attrs['id'])
        if 'href' in attrs:
            self.links.append(attrs['href'])
        if tag in ('script', 'link', 'img'):
            assert not attrs.get('src', '').startswith('http'), 'External runtime dependency'


def validate_source_hashes(language, text):
    paths = ['include/thread_pool.hpp', 'src/main.cpp', 'tests/thread_pool_tests.cpp', 'CMakeLists.txt'] if language == 'cpp' else ['FixedThreadPool.cs', 'Program.cs', 'SelfTests.cs', 'ThreadPoolCourse.csproj']
    for path in paths:
        assert hashlib.sha256((ROOT / language / path).read_bytes()).hexdigest() in text, f'Stale source: {path}'


def main():
    OUTPUT.mkdir(exist_ok=True)
    candidates = [Path('C:/Program Files/Google/Chrome/Application/chrome.exe'), Path('C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe')]
    browser_path = next((str(path) for path in candidates if path.exists()), None)
    assert browser_path, 'Install Chrome or Edge to run optional browser QA.'
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=browser_path, headless=True)
        context = browser.new_context(viewport={'width': 1440, 'height': 1100}, offline=True)
        for language in ('cpp', 'csharp'):
            path = ROOT / language / 'lesson.html'
            text = path.read_text(encoding='utf-8')
            parsed = Structure()
            parsed.feed(text)
            assert len(parsed.ids) == len(set(parsed.ids)), 'Duplicate IDs'
            for link in parsed.links:
                if link.startswith('#'):
                    assert link[1:] in parsed.ids, f'Broken anchor: {link}'
                elif not link.startswith(('https:', 'http:')):
                    assert (path.parent / link).resolve().exists(), f'Broken local link: {link}'
            validate_source_hashes(language, text)
            errors = []
            page = context.new_page()
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.goto(path.as_uri())
            page.screenshot(path=str(OUTPUT / f'{language}-desktop.png'))
            assert page.locator('section.chapter').count() == 11
            assert page.locator('#pool-status').inner_text().startswith('Running')
            page.locator('#submit-four').click()
            page.locator('#close-pool').click()
            assert '待取任务' in page.locator('#pool-workers').inner_text()
            assert page.locator('#submit-one').is_disabled()
            page.locator('#pool-step').click()
            assert 'queued=2 active=2 completed=0' in page.locator('#pool-log').inner_text()
            for _ in range(4):
                page.locator('#pool-step').click()
            assert page.locator('#pool-status').inner_text().startswith('Stopped')
            assert 'queued=0 active=0 completed=4' in page.locator('#pool-log').inner_text()
            page.locator('#reset-pool').click()
            assert page.locator('#submit-one').is_enabled()
            for mode, steps, expected in [('split', 4, '1'), ('atomic', 2, '2'), ('mutex', 4, '2')]:
                page.locator('#counter-mode').select_option(mode)
                page.locator('#counter-reset').click()
                for _ in range(steps):
                    page.locator('#counter-next').click()
                assert page.locator('#counter-value').inner_text() == expected
                assert page.locator('#counter-next').is_disabled()
            assert '3.33×' in page.locator('#calc-result').inner_text()
            page.locator('#calc-workers').fill('0')
            assert '有效范围' in page.locator('#calc-result').inner_text()
            page.locator('#calc-workers').fill('4')
            page.locator('#search').fill('mutex')
            assert page.locator('#search-results a').count() >= 2
            page.locator('#search').press('Escape')
            assert page.locator('#search-results').is_hidden()
            jump = page.locator('.source-jump').first
            target = jump.get_attribute('href')
            jump.click()
            assert page.locator(target).is_visible()
            assert page.locator('#source-0').get_attribute('open') is not None
            page.wait_for_function("selector => { const r = document.querySelector(selector).getBoundingClientRect(); return r.top >= 0 && r.bottom <= innerHeight; }", arg=target)
            page.screenshot(path=str(OUTPUT / f'{language}-source.png'))
            page.locator('#source-0 summary').click()
            page.set_viewport_size({'width': 390, 'height': 844})
            page.goto(path.as_uri())
            page.screenshot(path=str(OUTPUT / f'{language}-mobile.png'))
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth + 1'), 'Horizontal page overflow'
            page.locator('#model').scroll_into_view_if_needed()
            page.screenshot(path=str(OUTPUT / f'{language}-simulator-mobile.png'))
            page.locator('#submit-four').click()
            page.locator('#pool-step').click()
            assert 'active=2' in page.locator('#pool-log').inner_text()
            assert not errors, errors
            page.close()
            print(f'PASS {language}: anchors, local links, source hashes, offline loading, queue lifecycle, counter modes, calculator, search, source jump, 1440px/390px layouts')
        browser.close()


if __name__ == '__main__':
    main()
