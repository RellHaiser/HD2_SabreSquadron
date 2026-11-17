"""Browser smoke checks for the documentation fixture, not the game/model implementation.

Requires Python + playwright and its Chromium browser:
  python -m pip install playwright
  python -m playwright install chromium
  python docs/ui/check_campaign_preparation.py

Use --browser /absolute/path/to/chrome for an existing Chromium installation.
Optional --screenshots DIR writes original fixture screenshots (use ignored build/).
"""
import argparse
from pathlib import Path

from playwright.sync_api import expect, sync_playwright


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser")
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    if args.screenshots:
        args.screenshots.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True, **(
            {"executable_path": args.browser} if args.browser else {}))
        page = browser.new_page(viewport={"width": 1280, "height": 800})
        errors = []
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.goto(Path(__file__).with_name("campaign_preparation.html").as_uri())
        slots = page.locator("#squad button")
        expect(slots).to_have_count(4)
        expect(page.locator("#access")).to_have_text("Editing your soldier")
        slots.nth(1).click()
        expect(page.locator("#access")).to_contain_text("Read only")
        expect(page.locator("#equip")).to_be_disabled()
        slots.nth(2).click()
        expect(page.locator("#access")).to_have_text("Editing AI soldier")
        page.locator("#equip").click()
        expect(page.locator("#status")).to_contain_text("Request pending")
        expect(page.locator("#stock-count")).to_have_text("2")
        expect(page.locator("#inventory")).to_contain_text("0 medical kit(s)")
        expect(page.locator("#status")).to_contain_text("Edit accepted")
        expect(page.locator("#stock-count")).to_have_text("1")
        expect(page.locator("#inventory")).to_contain_text("1 medical kit(s)")
        page.locator("#return").click()
        expect(page.locator("#status")).to_contain_text("Edit accepted")
        expect(page.locator("#stock-count")).to_have_text("2")
        expect(page.locator("#inventory")).to_contain_text("0 medical kit(s)")

        page.locator("#role").select_option("guest")
        expect(page.locator("#access")).to_contain_text("Read only")
        expect(page.locator("#claim-action")).to_be_visible()
        expect(page.locator("#deploy")).to_be_hidden()
        slots.nth(1).click()
        expect(page.locator("#access")).to_have_text("Editing your soldier")
        page.locator("#equip").click()
        expect(page.locator("#inventory")).to_contain_text("1 medical kit(s)")
        page.locator("#disconnect").click()
        expect(page.locator("#inventory")).to_contain_text("1 medical kit(s)")
        expect(page.locator("#access")).to_contain_text("Read only")
        page.locator("#role").select_option("host")
        expect(page.locator("#access")).to_have_text("Editing AI soldier")

        page.locator("#reset").click()
        slots.nth(2).click()
        page.locator("#equip").click()
        page.locator("#claim").click()
        expect(page.locator("#access")).to_contain_text("Read only")
        expect(page.locator("#status")).to_contain_text("request rejected")
        expect(page.locator("#stock-count")).to_have_text("2")
        expect(page.locator("#inventory")).to_contain_text("0 medical kit(s)")

        page.locator("#scarce").click()
        expect(page.locator("#status")).to_contain_text("request rejected")
        expect(page.locator("#stock-count")).to_have_text("0")
        expect(page.locator("#inventory")).to_contain_text("1 medical kit(s)")
        slots.nth(1).click()
        expect(page.locator("#inventory")).to_contain_text("1 medical kit(s)")
        page.locator("#reset").click()
        page.locator("#reject").click()
        page.locator("#equip").click()
        expect(page.locator("#status")).to_contain_text("Edit rejected")
        expect(page.locator("#stock-count")).to_have_text("2")
        expect(page.locator("#inventory")).to_contain_text("1 medical kit(s)")

        page.locator("#loading").click()
        for action in ("equip", "return", "ready", "deploy"):
            expect(page.locator("#" + action)).to_be_disabled()
        page.locator("#loading").click()
        expect(page.locator("#equip")).to_be_enabled()
        page.locator("#remote-ready").click()
        page.locator("#ready").click()
        expect(page.locator("#deploy")).to_be_enabled()
        slots.nth(2).click()
        expect(page.locator("#deploy")).to_be_enabled()
        page.locator("#equip").click()
        expect(page.locator("#status")).to_contain_text("Edit accepted")
        expect(page.locator("#deploy")).to_be_disabled()
        page.locator("#remote-ready").click()
        page.locator("#ready").click()
        page.locator("#deploy").click()
        expect(page.locator("#phase")).to_have_text("Deploying")
        for action in ("equip", "return", "ready", "deploy"):
            expect(page.locator("#" + action)).to_be_disabled()

        page.locator("#restricted").click()
        page.locator("#role").select_option("guest")
        expect(page.locator("#readyline")).to_contain_text("You are spectating")
        expect(page.locator("#ready")).to_be_disabled()
        expect(page.locator("#equip")).to_be_disabled()
        slots.nth(1).click()
        expect(page.locator("#access")).to_contain_text("not deployed")
        expect(page.locator("#claim-action")).to_be_hidden()
        if args.screenshots:
            page.screenshot(path=str(args.screenshots / "restricted-guest.png"), full_page=True)

        page.locator("#reset").click()
        page.locator("#role").select_option("host")
        page.locator("#equip").focus()
        page.keyboard.press("Enter")
        expect(page.locator("#status")).to_contain_text("Edit accepted")
        expect(page.locator("#stock-count")).to_have_text("1")
        slots.nth(0).focus()
        page.keyboard.press("Space")
        expect(slots.nth(0)).to_be_focused()
        page.locator("#stats-toggle").click()
        expect(page.locator("#stats")).to_be_visible()

        for width, height in ((1280, 800), (1024, 768), (390, 844)):
            page.set_viewport_size({"width": width, "height": height})
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), f"Overflow at {width}"
            page.locator("#ready").scroll_into_view_if_needed()
            expect(page.locator("#ready")).to_be_in_viewport()
        page.set_viewport_size({"width": 1280, "height": 800})
        if args.screenshots:
            page.locator("#reset").click()
            page.screenshot(path=str(args.screenshots / "host.png"), full_page=True)
            page.locator("#role").select_option("guest")
            page.screenshot(path=str(args.screenshots / "guest-readonly.png"), full_page=True)
        page.once("dialog", lambda dialog: dialog.accept())
        page.locator("#leave").click()
        expect(page.locator("#phase")).to_have_text("Session ended")
        expect(page.locator("#equip")).to_be_disabled()
        assert not errors, errors
        print(f"PASS: permissions, atomic display, stock race, ownership race, rejection, loading, readiness, freeze, restrictions, keyboard, 3 viewport widths; Chromium {browser.version}; 0 page errors")
        browser.close()


if __name__ == "__main__":
    main()
