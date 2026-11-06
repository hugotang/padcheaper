package io.github.hugotang.padcheaper;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

public final class MainActivity extends Activity {
    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView text = new TextView(this);
        int margin = Math.round(24 * getResources().getDisplayMetrics().density);
        text.setPadding(margin, margin * 2, margin, margin); text.setTextSize(18);
        text.setText("PADCheaper " + BuildConfig.VERSION_NAME + "\n\n" +
            "在 LSPosed 管理器啟用此模組，作用域只選日服 PAD，然後完整重開遊戲。\n\n" +
            "啟用後，遊戲內會顯示「PC 診斷」按鈕。點選後可擷取接入狀態。\n\n" +
            "目前目標：日服 PAD " + BuildConfig.GAME_VERSION + "，需符合已核對的 APK。\n\n" +
            "這是接入診斷原型。讀盤、轉珠、技能與換隊尚未開放。更新模組後請完整重開遊戲。");
        setContentView(text);
    }
}
