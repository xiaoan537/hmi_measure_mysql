#include "calibration_widget.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

CalibrationWidget::CalibrationWidget(const core::AppConfig &cfg, QWidget *parent)
    : QWidget(parent), cfg_(cfg)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *top = new QHBoxLayout();
    lblConnPlc_ = new QLabel(QStringLiteral("PLC"), this);
    lblConnPlc_->setStyleSheet("background:#9ca3af;color:white;border-radius:10px;padding:4px 10px;font-weight:600;");
    lblStep_ = new QLabel(QStringLiteral("标定待机"), this);
    QFont f = lblStep_->font();
    f.setPointSize(18);
    f.setBold(true);
    lblStep_->setFont(f);
    top->addWidget(lblConnPlc_);
    top->addStretch(1);
    top->addWidget(lblStep_);
    top->addStretch(1);
    root->addLayout(top);

    auto *modeBox = new QGroupBox(QStringLiteral("标定流程"), this);
    auto *modeLay = new QVBoxLayout(modeBox);
    auto *typeRow = new QHBoxLayout();
    typeRow->addWidget(new QLabel(QStringLiteral("标定类型"), this));
    rbA_ = new QRadioButton(QStringLiteral("A 型标定"), this);
    rbB_ = new QRadioButton(QStringLiteral("B 型标定"), this);
    rbA_->setChecked(true);
    typeRow->addWidget(rbA_);
    typeRow->addWidget(rbB_);
    typeRow->addStretch(1);
    modeLay->addLayout(typeRow);

    lblMasterA_ = new QLabel(QStringLiteral("A 型标定件: CAL-A-001"), this);
    lblMasterB_ = new QLabel(QStringLiteral("B 型标定件: CAL-B-001"), this);
    lblSlot15_ = new QLabel(QStringLiteral("15 号槽位: 空"), this);
    modeLay->addWidget(lblMasterA_);
    modeLay->addWidget(lblMasterB_);
    modeLay->addWidget(lblSlot15_);

    auto *tip = new QLabel(QStringLiteral("说明：标定流程不扫码，标定件身份由 PC 本地配置管理；结果仅本地保存，不上传 MES。"), this);
    tip->setWordWrap(true);
    modeLay->addWidget(tip);
    root->addWidget(modeBox);

    auto *ops = new QGroupBox(QStringLiteral("操作"), this);
    auto *opsLay = new QHBoxLayout(ops);
    btnStart_ = new QPushButton(QStringLiteral("开始标定"), this);
    auto *btnRead = new QPushButton(QStringLiteral("读取测量包"), this);
    auto *btnAck = new QPushButton(QStringLiteral("写入 pc_ack"), this);
    auto *btnDemo = new QPushButton(QStringLiteral("演示"), this);
    opsLay->addWidget(btnStart_);
    opsLay->addWidget(btnRead);
    opsLay->addWidget(btnAck);
    opsLay->addWidget(btnDemo);
    opsLay->addStretch(1);
    root->addWidget(ops);

    listMessages_ = new QListWidget(this);
    root->addWidget(listMessages_, 1);

    connect(btnStart_, &QPushButton::clicked, this, [this]{
        QVariantMap args;
        args.insert(QStringLiteral("part_type"), selectedMasterTypeText());
        args.insert(QStringLiteral("mode_arg"), static_cast<int>(selectedMasterTypeArg()));
        emit uiCommandRequested(QStringLiteral("START_CALIBRATION"), args);
        listMessages_->addItem(QStringLiteral("开始标定：%1").arg(selectedMasterTypeText()));
    });
    connect(btnRead, &QPushButton::clicked, this, [this]{
        emit requestReadMailbox();
        listMessages_->addItem(QStringLiteral("已请求读取标定测量包。"));
    });
    connect(btnAck, &QPushButton::clicked, this, [this]{
        emit requestAckMailbox();
        listMessages_->addItem(QStringLiteral("已请求写入 pc_ack。"));
    });
    connect(btnDemo, &QPushButton::clicked, this, [this]{
        setPlcConnected(true);
        setStepState(200);
        setTrayPresentMask(1u << 15);
        listMessages_->addItem(QStringLiteral("演示：15 号槽位有料，可开始标定。"));
    });

    setPlcConnected(false);
    setStepState(200);
    setTrayPresentMask(0);
}

void CalibrationWidget::setPlcConnected(bool ok)
{
    lblConnPlc_->setStyleSheet(ok ?
        "background:#22c55e;color:white;border-radius:10px;padding:4px 10px;font-weight:600;" :
        "background:#9ca3af;color:white;border-radius:10px;padding:4px 10px;font-weight:600;");
}

void CalibrationWidget::setStepState(quint16 step)
{
    lblStep_->setText(stepText(step));
}

void CalibrationWidget::setTrayPresentMask(quint16 mask)
{
    trayPresentMask_ = mask;
    refreshSlot15State();
}

void CalibrationWidget::setMailboxReady(bool ready)
{
    listMessages_->addItem(ready ? QStringLiteral("PLC 已冻结标定测量包，可读取。")
                                 : QStringLiteral("标定测量包已清空。"));
}

void CalibrationWidget::setMasterPartIds(const QString &aPartId, const QString &bPartId)
{
    lblMasterA_->setText(QStringLiteral("A 型标定件: %1").arg(aPartId));
    lblMasterB_->setText(QStringLiteral("B 型标定件: %1").arg(bPartId));
}

QString CalibrationWidget::selectedMasterTypeText() const
{
    return rbB_ && rbB_->isChecked() ? QStringLiteral("B") : QStringLiteral("A");
}

quint32 CalibrationWidget::selectedMasterTypeArg() const
{
    return rbB_ && rbB_->isChecked() ? 2u : 1u;
}

void CalibrationWidget::refreshSlot15State()
{
    const bool loaded = ((trayPresentMask_ >> 15) & 0x1) != 0;
    lblSlot15_->setText(loaded ? QStringLiteral("15 号槽位: 有料") : QStringLiteral("15 号槽位: 空"));
}

QString CalibrationWidget::stepText(quint16 step) const
{
    switch (step) {
    case 200: return QStringLiteral("标定待上料(15号槽)");
    case 210: return QStringLiteral("等待 PC 确认");
    case 220: return QStringLiteral("标定测量中");
    case 230: return QStringLiteral("等待 PC 读取");
    case 240: return QStringLiteral("标定完成");
    case 900: return QStringLiteral("报警");
    case 910: return QStringLiteral("急停");
    default: return QStringLiteral("标定流程(%1)").arg(step);
    }
}
