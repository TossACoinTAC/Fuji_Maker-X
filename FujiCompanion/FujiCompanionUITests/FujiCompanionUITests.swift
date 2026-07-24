import XCTest

final class FujiCompanionUITests: XCTestCase {
    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    @MainActor
    func testMockFoodRequestCompletesNavigationFlow() throws {
        let app = launchApp()

        completeSelection(in: app)

        XCTAssertTrue(app.staticTexts["路线已准备好"].waitForExistence(timeout: 3))
        XCTAssertTrue(app.staticTexts["已在 Apple 地图中打开步行路线"].exists)
    }

    @MainActor
    func testNavigationFailureReportsRealOutcome() throws {
        let app = launchApp(environment: ["UITEST_NAVIGATION_FAILURE": "1"])

        completeSelection(in: app)

        XCTAssertTrue(app.staticTexts["这次没有完成"].waitForExistence(timeout: 3))
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
        XCTAssertTrue(clearButton.waitForExistence(timeout: 3))
        clearButton.tap()
        app.buttons["清除"].tap()

        XCTAssertTrue(app.buttons["privacy.accept"].waitForExistence(timeout: 3))
    }

    @MainActor
    func testOfflineStateIsVisible() throws {
        let app = launchApp(environment: ["UITEST_OFFLINE": "1"])
        let status = app.otherElements["fuji.status"]

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
    private func completeSelection(in app: XCUIApplication) {
        let simulate = app.buttons["debug.simulateFoodRequest"]
        XCTAssertTrue(simulate.waitForExistence(timeout: 3))
        simulate.tap()

        let search = app.buttons["criteria.search"]
        XCTAssertTrue(search.waitForExistence(timeout: 3))
        search.tap()

        let select = app.buttons["restaurant.select.0"]
        XCTAssertTrue(select.waitForExistence(timeout: 5))
        select.tap()

        let confirm = app.buttons["navigation.confirm"]
        XCTAssertTrue(confirm.waitForExistence(timeout: 3))
        confirm.tap()
    }
}
