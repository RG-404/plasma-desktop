
loadTemplate("org.kde.plasma.desktop.defaultPanel")

var desktopsArray = desktops();
for( var j = 0; j < desktopsArray.length; j++) {
    desktopsArray[j].wallpaperPlugin = 'org.kde.image';
    //var clock = desktopsArray[j].addWidget("org.kde.plasma.analogclock");
}

