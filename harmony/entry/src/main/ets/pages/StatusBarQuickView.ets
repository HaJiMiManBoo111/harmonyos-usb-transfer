/*
 * Flux -- HarmonyOS Flux client status-bar quick-operation page
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

import { desktopManager } from '../statusbar/DeskflowStatusStore';

@Entry
@Component
struct StatusBarQuickView {
  @State state: number = 0;
  @State statusText: string = '未连接';

  aboutToAppear(): void {
    desktopManager.registerStateListener((s: number, t: string) => {
      this.state = s;
      this.statusText = t;
    });
    this.state = desktopManager.getState();
    this.statusText = desktopManager.getStatusText();
  }

  build() {
    Column({ space: 12 }) {
      Row({ space: 10 }) {
        Circle({ width: 10, height: 10 })
          .fill(
            this.state === 1 ? '#FF9F0A' : this.state === 2 ? '#34C759' :
              this.state === 3 ? '#FF3B30' : '#8E8E93'
          )
          .shadow({ radius: 6, color: '#1A000000', offsetY: 0 })
        Text('Flux')
          .fontSize(17)
          .fontWeight(FontWeight.Medium)
          .fontColor('#1C1C1E')
      }
      .width('100%')
      .justifyContent(FlexAlign.Center)

      Text(this.statusText)
        .fontSize(15)
        .fontColor(
          this.state === 1 ? '#FF9F0A' : this.state === 2 ? '#34C759' :
            this.state === 3 ? '#FF3B30' : '#8E8E93'
        )
        .maxLines(2)
        .textOverflow({ overflow: TextOverflow.Ellipsis })
        .textAlign(TextAlign.Center)

      Text('点右键菜单可：打开主界面 / 连接 / 断开')
        .fontSize(13)
        .fontColor('#8E8E93')
        .textAlign(TextAlign.Center)
    }
    .width('100%')
    .padding(20)
    .backgroundColor('#FFFFFF')
    .borderRadius(18)
    .shadow({ radius: 12, color: '#1A000000', offsetY: 2 })
    .alignItems(HorizontalAlign.Center)
  }
}
