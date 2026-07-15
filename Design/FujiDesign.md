## v1
基础环境：DNESP32S3 + 小智AI

A cute pet, and also a helpful daily companion for everyone who loves cute things.

It's a little fairy sitting on your shoulder with magnet.

Core function: It's cute, very cute. It feels warm and soft. It has cute facial expressions to your words. It may also have some cute actions.
Make a bad mood vanish, make a good day even better.


软萌 团子 bot  (Fuji) 
尺寸：较小 可以磁吸 戴在肩膀上（？ 可出门 更像饰品兼小工具/赛博宠物？
性格：软萌，古灵精怪等
目标用户：二次元爱好者
典型场景：日常、友人出行等

网络：连接手机热点
语音交互：关键词识别+自由对话等

Silent use / Bluetooth:
- Fuji should connect to the phone over Bluetooth Low Energy for control, status, and requests.
- The phone should remain the Bluetooth hub: it can route Fuji's response audio to the user's connected earphones and keep the music player on the phone as the media source.
- The phone companion should support a clear output route, music pause/duck/restore policy, and basic play/pause/next commands where the phone platform permits them.
- Direct Fuji-to-earphone audio is not a first-release promise. The ESP32-S3 design should be treated as a BLE control endpoint unless a compatible Bluetooth audio source profile is separately proven.
- If earphones disconnect, Fuji should fall back to face/haptic feedback and must not suddenly speak private content through its own speaker.

It's also helpful. Picture this: You are hanging out with your friends.
It's time for meal, someone says: Well, what to eat? Then everyone answers: Well, what to eat?

Then instead of rolling a dice, the cute fairy will answer you with some recommends.

You need no operation on the phone or even the watch. Just a conversation and all done. It will suggest the restaurant, it will trigger the navigation, then it will lead the way with voice.

比较强的专门功能：
- 是啊吃什么：根据历史喜好等信息筛选 自动搜索附近的觅食地点
- 语音导航播报：连接导航软件 可以启动导航、播报导航信息
- 翻译、日程提醒等
- 卖萌（核心功能（？  ： 可能需要表情系统、触摸感应、重力/姿态感应、视觉等

软萌的核心决定交互时的良好体验，而一些简单实用小功能实际确保高使用率，而非昙花一现的装饰品

难点：
- 需要与其他软件深度交互，如何交互
- 需要续航，如何供电
