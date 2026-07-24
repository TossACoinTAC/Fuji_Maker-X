import SwiftUI

@main
struct FujiCompanionApp: App {
    @State private var model: FujiAppModel

    init() {
        _model = State(initialValue: FujiAppModel(environment: .live()))
    }

    var body: some Scene {
        WindowGroup {
            ContentView(model: model)
                .task {
                    model.start()
                }
        }
    }
}
