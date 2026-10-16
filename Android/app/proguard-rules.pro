# Keep JavascriptInterface methods for WebView bridge if needed
-keepattributes *Annotation*
-keepclassmembers class * {
    @android.webkit.JavascriptInterface <methods>;
}
