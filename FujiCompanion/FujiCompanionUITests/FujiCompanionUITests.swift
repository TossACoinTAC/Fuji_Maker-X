import XCTest

final class FujiCompanionUITests: XCTestCase {
    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    @MainActor
    func testMockFoodRequestCompletesNavigationFlow() throws {
        let app = launchApp()

        let confirm = completeSelection(in: app)

        let success = app.staticTexts["路线已准备好"]
        tap(confirm, until: success)
        XCTAssertTrue(app.staticTexts["已在 Apple 地图中打开步行路线"].exists)
    }

    @MainActor
    func testNavigationFailureReportsRealOutcome() throws {
        let app = launchApp(environment: ["UITEST_NAVIGATION_FAILURE": "1"])

        let confirm = completeSelection(in: app)

        tap(confirm, until: app.staticTexts["这次没有完成"])
        let failure = app.staticTexts.matching(
            NSPredicate(format: "label CONTAINS '测试地图不可用'")
        ).firstMatch
        XCTAssertTrue(failure.exists)
    }

    @MainActor
    func testPrivacyDataClearResetsSessionAndConsent() throws {
        let app = launchApp()
        let settingsTab = app.tabBars.buttons["设置"]
        XCTAssertTrue(settingsTab.waitForExistence(timeout: 3))
        settingsTab.tap()

        let clearButton = app.buttons["privacy.clearData"]
        reveal(clearButton, in: app)
        let confirmClear = app.buttons["清除"]
        tap(clearButton, until: confirmClear, timeout: 2)
        confirmClear.tap()
        var dialogDismissed = confirmClear.waitForNonExistence(timeout: 2)
        if !dialogDismissed, confirmClear.exists, confirmClear.isHittable {
            confirmClear.tap()
            dialogDismissed = confirmClear.waitForNonExistence(timeout: 2)
        }
        XCTAssertTrue(dialogDismissed)

        let acceptButton = app.buttons["privacy.accept"]
        for _ in 0..<3 {
            app.swipeDown()
        }
        reveal(acceptButton, in: app)
    }

    @MainActor
    func testOfflineStateIsVisible() throws {
        let app = launchApp(environment: ["UITEST_OFFLINE": "1"])
        let status = app.staticTexts["fuji.status"]

        XCTAssertTrue(status.waitForExistence(timeout: 3))
        XCTAssertTrue(status.label.contains("未连接"))
    }

    @MainActor
    func testLaunchPerformance() throws {
        measure(metrics: [XCTApplicationLaunchMetric()]) {
            _ = launchApp()
        }
    }

    @MainActor
    private func launchApp(environment: [String: String] = [:]) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchEnvironment["UITEST_MODE"] = "1"
        environment.forEach { app.launchEnvironment[$0.key] = $0.value }
        app.launchArguments += ["-AppleLanguages", "(zh-Hans)", "-AppleLocale", "zh_CN"]
        app.launch()
        return app
    }

    @MainActor
    private func completeSelection(in app: XCUIApplication) -> XCUIElement {
        let status = app.staticTexts["fuji.status"]
        XCTAssertTrue(status.waitForExistence(timeout: 3))
        let connected = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "label CONTAINS '演示设备已连接'"),
            object: status
        )
        XCTAssertEqual(XCTWaiter.wait(for: [connected], timeout: 3), .completed)

        let simulate = app.buttons["debug.simulateFoodRequest"]
        XCTAssertTrue(simulate.waitForExistence(timeout: 3))

        let search = app.buttons["criteria.search"]
        tap(simulate, until: search)

        let select = app.buttons["restaurant.select.0"]
        tap(search, until: select, timeout: 5)

        let confirm = app.buttons["navigation.confirm"]
        tap(select, until: confirm)
        return confirm
    }

    @MainActor
    private func tap(
        _ button: XCUIElement,
        until target: XCUIElement,
        timeout: TimeInterval = 3
    ) {
        XCTAssertTrue(button.exists)
        XCTAssertTrue(button.isHittable)
        button.tap()

        var transitioned = target.waitForExistence(timeout: timeout)
        if !transitioned, button.exists, button.isHittable {
            button.tap()
            transitioned = target.waitForExistence(timeout: timeout)
        }
        XCTAssertTrue(transitioned)
    }

    @MainActor
    private func reveal(_ element: XCUIElement, in app: XCUIApplication) {
        for _ in 0..<4 where !element.exists {
            app.swipeUp()
        }
        XCTAssertTrue(element.waitForExistence(timeout: 2))
        XCTAssertTrue(element.isHittable)
    }
}
