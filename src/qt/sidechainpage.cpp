// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sidechainpage.h>
#include <qt/forms/ui_sidechainpage.h>

#include <qt/confgeneratordialog.h>
#include <qt/drivenetunits.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/sidechainescrowtablemodel.h>
#include <qt/sidechainwithdrawaltablemodel.h>
#include <qt/walletmodel.h>

#include <base58.h>
#include <coins.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <init.h>
#include <miner.h>
#include <net.h>
#include <primitives/block.h>
#include <txdb.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QMessageBox>
#include <QScrollBar>
#include <QStackedWidget>

#include <sstream>

SidechainPage::SidechainPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SidechainPage)
{
    ui->setupUi(this);

    ui->listWidgetSidechains->setIconSize(QSize(32, 32));

    // Setup Sidechains list widget
    for (const Sidechain& s : ValidSidechains) {
        QListWidgetItem *item = new QListWidgetItem(ui->listWidgetSidechains);

        // Set icon
        QIcon icon(SidechainIcons[s.nSidechain]);
        item->setIcon(icon);

        // Set text
        item->setText(QString::fromStdString(s.GetSidechainName()));
        QFont font = item->font();
        font.setPointSize(16);
        item->setFont(font);

        ui->listWidgetSidechains->addItem(item);
    }

    // Setup sidechain selection combo box
    for (const Sidechain& s : ValidSidechains) {
        ui->comboBoxSidechains->addItem(QString::fromStdString(s.GetSidechainName()));
    }

    // Initialize models
    escrowModel = new SidechainEscrowTableModel(this);
    withdrawalModel = new SidechainWithdrawalTableModel(this);

    // Setup the tables
    SetupTables();

    ui->listWidgetSidechains->setCurrentRow(0);
}

SidechainPage::~SidechainPage()
{
    delete ui;
}

void SidechainPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if (model && model->getOptionsModel())
    {
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this,
                SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
    }
}

void SidechainPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance,
                               const CAmount& immatureBalance, const CAmount& watchOnlyBalance,
                               const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    const CAmount& pending = immatureBalance + unconfirmedBalance;
    //ui->available->setText(BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways));
    //ui->pending->setText(BitcoinUnits::formatWithUnit(unit, pending, false, BitcoinUnits::separatorAlways));
}

void SidechainPage::SetupTables()
{
    // Add models to table views
    ui->tableViewEscrow->setModel(escrowModel);
    ui->tableViewWT->setModel(withdrawalModel);

    // Resize cells (in a backwards compatible way)
#if QT_VERSION < 0x050000
    ui->tableViewEscrow->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
    ui->tableViewWT->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
#else
    ui->tableViewEscrow->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableViewWT->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
#endif

    // Don't stretch last cell of horizontal header
    ui->tableViewEscrow->horizontalHeader()->setStretchLastSection(false);
    ui->tableViewWT->horizontalHeader()->setStretchLastSection(false);

    // Hide vertical header
    ui->tableViewEscrow->verticalHeader()->setVisible(false);
    ui->tableViewWT->verticalHeader()->setVisible(false);

    // Left align the horizontal header text
    ui->tableViewEscrow->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    ui->tableViewWT->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);

    // Set horizontal scroll speed to per 3 pixels (very smooth, default is awful)
    ui->tableViewEscrow->horizontalHeader()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->tableViewWT->horizontalHeader()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->tableViewEscrow->horizontalHeader()->horizontalScrollBar()->setSingleStep(3); // 3 Pixels
    ui->tableViewWT->horizontalHeader()->horizontalScrollBar()->setSingleStep(3); // 3 Pixels

    // Disable word wrap
    ui->tableViewEscrow->setWordWrap(false);
    ui->tableViewWT->setWordWrap(false);
}
void SidechainPage::on_pushButtonDeposit_clicked()
{
    QMessageBox messageBox;

    unsigned int nSidechain = ui->comboBoxSidechains->currentIndex();

    if (!IsSidechainNumberValid(nSidechain)) {
        // Should never be displayed
        messageBox.setWindowTitle("Invalid sidechain selected");
        messageBox.exec();
        return;
    }

#ifdef ENABLE_WALLET
    if (vpwallets.empty()) {
        messageBox.setWindowTitle("Wallet Error!");
        messageBox.setText("No active wallets to create the deposit.");
        messageBox.exec();
        return;
    }

    if (vpwallets[0]->IsLocked()) {
        // Locked wallet message box
        messageBox.setWindowTitle("Wallet locked!");
        messageBox.setText("Wallet must be unlocked to create sidechain deposit.");
        messageBox.exec();
        return;
    }
#endif

    if (!validateDepositAmount()) {
        // Invalid deposit amount message box
        messageBox.setWindowTitle("Invalid deposit amount!");
        QString error = "Check the amount you have entered and try again.\n\n";
        error += "Your deposit must be > 0.00001 BTC to cover the sidechain ";
        error += "deposit fee. If the output amount is dust after paying the ";
        error += "fee, you will not receive anything on the sidechain.\n";
        messageBox.setText(error);
        messageBox.exec();
        return;
    }

    // Get keyID
    CBitcoinAddress address(ui->payTo->text().toStdString());
    CKeyID keyID;
    if (!address.GetKeyID(keyID)) {
        // Invalid address message box
        messageBox.setWindowTitle("Invalid Bitcoin address!");
        messageBox.setText("Check the address you have entered and try again.");
        messageBox.exec();
        return;
    }

#ifdef ENABLE_WALLET
    // Attempt to create the deposit
    const CAmount& nValue = ui->payAmount->value();
    CTransactionRef tx;
    std::string strFail = "";
    if (!vpwallets.empty()) {
        if (!vpwallets[0]->CreateSidechainDeposit(tx, strFail, nSidechain, nValue, keyID)) {
            // Create transaction error message box
            messageBox.setWindowTitle("Creating deposit transaction failed!");
            QString createError = "Error creating transaction!\n\n";
            createError += QString::fromStdString(strFail);
            messageBox.setText(createError);
            messageBox.exec();
            return;
        }
    }

    // Successful deposit message box
    messageBox.setWindowTitle("Deposit transaction created!");
    QString result = "Deposited to " + QString::fromStdString(GetSidechainName(nSidechain));
    result += "\n";
    result += "txid: " + QString::fromStdString(tx->GetHash().ToString());
    result += "\n";
    result += "Amount deposited: ";
    result += BitcoinUnits::formatWithUnit(BitcoinUnit::BTC, nValue, false, BitcoinUnits::separatorAlways);
    messageBox.setText(result);
    messageBox.exec();
#endif
}

void SidechainPage::on_pushButtonPaste_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SidechainPage::on_pushButtonClear_clicked()
{
    ui->payTo->clear();
}

void SidechainPage::on_pushButtonGenerateConfig_clicked()
{
    // Show configuration generator dialog
    ConfGeneratorDialog *dialog = new ConfGeneratorDialog(this);
    dialog->exec();
}

void SidechainPage::on_comboBoxSidechains_currentIndexChanged(const int i)
{
    if (!IsSidechainNumberValid(i))
        return;

    ui->listWidgetSidechains->setCurrentRow(i);

    // Update deposit button text
    QString strSidechain = QString::fromStdString(GetSidechainName(i));
    QString str = "Deposit to: " + strSidechain;
    ui->pushButtonDeposit->setText(str);
}

void SidechainPage::on_listWidgetSidechains_doubleClicked(const QModelIndex& i)
{
    ui->comboBoxSidechains->setCurrentIndex(i.row());
}

bool SidechainPage::validateDepositAmount()
{
    if (!ui->payAmount->validate()) {
        ui->payAmount->setValid(false);
        return false;
    }

    // Sending a zero amount is invalid
    if (ui->payAmount->value(0) <= 0) {
        ui->payAmount->setValid(false);
        return false;
    }

    // Reject dust outputs:
    if (GUIUtil::isDust(ui->payTo->text(), ui->payAmount->value())) {
        ui->payAmount->setValid(false);
        return false;
    }

    // Reject deposits which cannot cover sidechain fee
    if (ui->payAmount->value() < SIDECHAIN_DEPOSIT_FEE) {
        ui->payAmount->setValid(false);
        return false;
    }

    // Reject deposits which would net the user no payout on the sidechain
    if (GUIUtil::isDust(ui->payTo->text(), ui->payAmount->value() - SIDECHAIN_DEPOSIT_FEE)) {
        ui->payAmount->setValid(false);
        return false;
    }

    return true;
}
