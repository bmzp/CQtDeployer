OTHER_FILES += \
    $$PWD/*.md \
    $$PWD/*.sh \
    $$PWD/scripts/*.py \
    $$PWD/packages/QIF/meta/*.xml

win32:PLATFORM = windows
unix: PLATFORM = linux

win32:PY = python
unix: PY = python3

qif.commands= $$PY $$PWD/scripts/QIF.py $$PLATFORM 3.2.0 $$PWD/packages/QIF/data

!isEmpty( ONLINE ) {

    message(prepare release QIF)
    deployOffline.depends += qif
    buildSnap.depends += qif
}

QMAKE_EXTRA_TARGETS += \
    qif
