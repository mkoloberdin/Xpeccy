#include "xcore/xcore.h"
#include "dbg_brkpoints.h"

// Model

xBreakListModel::xBreakListModel(QObject* par):QAbstractTableModel(par) {

}

int xBreakListModel::rowCount(const QModelIndex&) const {
	if (!conf.prof.cur) return 0;
	return conf.prof.cur->brkList.size();
}

int xBreakListModel::columnCount(const QModelIndex&) const {
	return 5;
}

QString brkGetString(xBrkPoint brk) {
	QString res;
	switch(brk.type) {
		case BRK_CPUADR:
			res = QString("CPU:%0").arg(gethexword(brk.adr));
			if (brk.eadr > brk.adr) {
				res.append(QString("-%0").arg(gethexword(brk.eadr)));
			}
			break;
		case BRK_IOPORT:
			res = QString("IO:%0 mask %1").arg(gethexword(brk.adr)).arg(gethexword(brk.mask));
			break;
		case BRK_MEMRAM:
			if (brk.eadr > brk.adr) {
				res = QString("RAM:%0-%1 [%2:%3-%4:%5]").arg(gethex6(brk.adr)).arg(gethex6(brk.eadr)).arg(gethexbyte(brk.adr >> 14)).arg(gethexword(brk.adr & 0x3fff)).arg(gethexbyte(brk.eadr >> 14)).arg(gethexword(brk.eadr & 0x3fff));
			} else {
				res = QString("RAM:%0 [%1:%2]").arg(gethex6(brk.adr)).arg(gethexbyte(brk.adr >> 14)).arg(gethexword(brk.adr & 0x3fff));
			}
			break;
		case BRK_MEMROM:
			if (brk.eadr > brk.adr) {
				res = QString("ROM:%0-%1 [%2:%3-%4:%5]").arg(gethex6(brk.adr)).arg(gethex6(brk.eadr)).arg(gethexbyte(brk.adr >> 14)).arg(gethexword(brk.adr & 0x3fff)).arg(gethexbyte(brk.eadr >> 14)).arg(gethexword(brk.eadr & 0x3fff));
			} else {
				res = QString("ROM:%0 [%1:%2]").arg(gethex6(brk.adr)).arg(gethexbyte(brk.adr >> 14)).arg(gethexword(brk.adr & 0x3fff));
			}
			break;
		case BRK_MEMSLT:
			res = QString("SLT:%0 [%1:%2]").arg(gethex6(brk.adr)).arg(gethexbyte(brk.adr >> 14)).arg(gethexword(brk.adr & 0x3fff));
			break;
		case BRK_MEMEXT:
			res = QString("EXT:%0 [%1:%2]").arg(gethex6(brk.adr)).arg(gethexbyte(brk.adr >> 14)).arg(gethexword(brk.adr & 0x3fff));
			break;
		case BRK_IRQ:
			res = QString("IRQ");
			break;
	}
	return res;
}

QVariant xBreakListModel::data(const QModelIndex& idx, int role) const {
	QVariant res;
	if (!idx.isValid()) return res;
	int row = idx.row();
	int col = idx.column();
	if ((col < 0) || (col >= columnCount())) return res;
	if ((row < 0) || (row >= rowCount())) return res;
	xBrkPoint brk = conf.prof.cur->brkList[row];
	switch (role) {
		case Qt::CheckStateRole:
			switch(col) {
				case 0: res = brk.off ? Qt::Unchecked : Qt::Checked; break;
				case 1: if ((brk.type != BRK_IRQ) && (brk.type != BRK_IOPORT)) res = brk.fetch ? Qt::Checked : Qt::Unchecked; break;
				case 2: if (brk.type != BRK_IRQ) res = brk.read ? Qt::Checked : Qt::Unchecked; break;
				case 3: if (brk.type != BRK_IRQ) res = brk.write ? Qt::Checked : Qt::Unchecked; break;
			}
			break;
		case Qt::DisplayRole:
			switch(col) {
				case 4: res = brkGetString(brk); break;
			}
			break;
	}
	return res;
}

QVariant xBreakListModel::headerData(int sect, Qt::Orientation ornt, int role) const {
	QVariant res;
	switch(ornt) {
		case Qt::Horizontal:
			if (sect < 0) break;
			if (sect >= columnCount()) break;
			switch(role) {
				case Qt::DisplayRole:
					switch(sect) {
						case 0: res = "On"; break;
						case 1: res = "F"; break;
						case 2: res = "R"; break;
						case 3: res = "W"; break;
						case 4: res = "Addr"; break;
					}
					break;
			}
			break;
		case Qt::Vertical:
			break;
	}
	return res;
}

bool xbsOff(const xBrkPoint bpa, const xBrkPoint bpb) {return (bpa.off && !bpb.off);}
bool xbsFe(const xBrkPoint bpa, const xBrkPoint bpb) {return (bpa.fetch && !bpb.fetch);}
bool xbsRd(const xBrkPoint bpa, const xBrkPoint bpb) {return (bpa.read && !bpb.read);}
bool xbsWr(const xBrkPoint bpa, const xBrkPoint bpb) {return (bpa.write && !bpb.write);}
bool xbsName(const xBrkPoint bpa, const xBrkPoint bpb) {
	return brkGetString(bpa) < brkGetString(bpb);
}

void xBreakListModel::sort(int col, Qt::SortOrder ord) {
	if (!conf.prof.cur) return;
	switch(col) {
		case 0: std::sort(conf.prof.cur->brkList.begin(), conf.prof.cur->brkList.end(), xbsOff); break;
		case 1: std::sort(conf.prof.cur->brkList.begin(), conf.prof.cur->brkList.end(), xbsFe); break;
		case 2: std::sort(conf.prof.cur->brkList.begin(), conf.prof.cur->brkList.end(), xbsRd); break;
		case 3: std::sort(conf.prof.cur->brkList.begin(), conf.prof.cur->brkList.end(), xbsWr); break;
		case 4: std::sort(conf.prof.cur->brkList.begin(), conf.prof.cur->brkList.end(), xbsName); break;
	}
	emit dataChanged(index(0,0), index(rowCount() - 1, columnCount() - 1));
}

void xBreakListModel::updateCell(int row, int col) {
	emit dataChanged(index(row, col), index(row, col));
}

void xBreakListModel::update() {
	emit endResetModel();
}

// Widget

xBreakTable::xBreakTable(QWidget* p):QTableView(p) {
	model = new xBreakListModel();
	setModel(model);
	setColumnWidth(0, 30);
	setColumnWidth(1, 30);
	setColumnWidth(2, 30);
	setColumnWidth(3, 30);
	connect(this, SIGNAL(clicked(QModelIndex)), this, SLOT(onCellClick(QModelIndex)));
	connect(this, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(onDoubleClick(QModelIndex)));
}

void xBreakTable::update() {
	model->update();
	QTableView::update();
}

void xBreakTable::keyPressEvent(QKeyEvent* ev) {
	ev->ignore();
}

void xBreakTable::onCellClick(QModelIndex idx) {
	if (!idx.isValid()) return;
	int row = idx.row();
	int col = idx.column();
	xProfile* prf = conf.prof.cur;
	xBrkPoint* brk = &prf->brkList[row];
	switch(col) {
		case 0: brk->off ^= 1; break;
		case 1: brk->fetch ^= 1; break;
		case 2: brk->read ^= 1; break;
		case 3: brk->write ^= 1; break;
	}
	// brkInstall(prf->brkList[row], 0);
	brkInstallAll();
	model->updateCell(row, col);
	emit rqDasmDump();
}

void xBreakTable::onDoubleClick(QModelIndex idx) {
	if (!idx.isValid()) return;
	int row = idx.row();
	xBrkPoint bp = conf.prof.cur->brkList[row];
	int adr = -1;
	switch(bp.type) {
		case BRK_CPUADR: adr = bp.adr; break;
		case BRK_MEMRAM: adr = memFindAdr(conf.prof.cur->zx->mem, MEM_RAM, bp.adr); break;
		case BRK_MEMROM: adr = memFindAdr(conf.prof.cur->zx->mem, MEM_ROM, bp.adr); break;
	}
	if (adr < 0) return;
	emit rqDisasm(adr);
}

// Dialog

xBrkManager::xBrkManager(QWidget* p):QDialog(p) {
	ui.setupUi(this);

	ui.brkType->addItem("ADR bus (MEM)", BRK_CPUADR);
	ui.brkType->addItem("ADR bus (IO)", BRK_IOPORT);
	ui.brkType->addItem("RAM cell", BRK_MEMRAM);
	ui.brkType->addItem("ROM cell", BRK_MEMROM);
	ui.brkType->addItem("SLT cell", BRK_MEMSLT);
	ui.brkType->addItem("IRQ", BRK_IRQ);

	ui.brkAdrEnd->setMin(0x0000);
	ui.brkAdrEnd->setMax(0xffff);
	ui.leStartOffset->setMin(0);
	ui.leStartOffset->setMax(0x3fff);
	ui.leEndOffset->setMin(0);
	ui.leEndOffset->setMax(0x3fff);
	ui.leValue->setMin(0);
	ui.leValue->setMax(0xff);
	ui.leValMask->setMin(0);
	ui.leValMask->setMax(0xff);
	ui.leValMask->setValue(0xff);

	connect(ui.brkBank, SIGNAL(valueChanged(int)), this, SLOT(bnkChanged(int)));
	connect(ui.leStartOffset,SIGNAL(valueChanged(int)),this,SLOT(startOffChanged(int)));
	connect(ui.brkAdrHex,SIGNAL(valueChanged(int)),this,SLOT(startAbsChanged(int)));
	connect(ui.leEndOffset,SIGNAL(valueChanged(int)),this,SLOT(endOffChanged(int)));
	connect(ui.brkAdrEnd,SIGNAL(valueChanged(int)),this,SLOT(endAbsChanged(int)));

	connect(ui.brkType, SIGNAL(currentIndexChanged(int)), this, SLOT(chaType(int)));
//	connect(ui.brkAdrHex, SIGNAL(valueChanged(int)), ui.brkAdrEnd, SLOT(setMin(int)));
	connect(ui.pbOK, SIGNAL(clicked()), this, SLOT(confirm()));
}

void xBrkManager::bnkChanged(int v) {
	ui.brkAdrHex->blockSignals(true);
	ui.brkAdrEnd->blockSignals(true);
	ui.brkAdrHex->setValue((v << 14) | (ui.leStartOffset->getValue() & 0x3fff));
	ui.brkAdrEnd->setMin((v << 14) | (ui.leStartOffset->getValue() & 0x3fff));
	ui.brkAdrEnd->setValue((v << 14) | (ui.leEndOffset->getValue() & 0x3fff));
	ui.brkAdrHex->blockSignals(false);
	ui.brkAdrEnd->blockSignals(false);
}

void xBrkManager::startOffChanged(int v) {
	ui.brkAdrHex->blockSignals(true);
	v = (ui.brkBank->value() << 14) | (v & 0x3fff);
	ui.brkAdrHex->setValue(v);
	ui.brkAdrEnd->setMin(v);
	ui.brkAdrEnd->setValue(v);
	ui.brkAdrHex->blockSignals(false);
}

void xBrkManager::startAbsChanged(int v) {
	ui.brkBank->blockSignals(true);
	ui.leStartOffset->blockSignals(true);
	ui.brkBank->setValue(v >> 14);
	ui.leStartOffset->setValue(v & 0x3fff);
	ui.brkBank->blockSignals(false);
	ui.leStartOffset->blockSignals(false);
	ui.brkAdrEnd->setMin(v);
	ui.brkAdrEnd->setValue(v);
}

void xBrkManager::endOffChanged(int v) {
	ui.brkAdrEnd->blockSignals(true);
	ui.brkAdrEnd->setValue((ui.brkBank->value() << 14) | (v & 0x3fff));
	ui.brkAdrEnd->blockSignals(false);
}

void xBrkManager::endAbsChanged(int v) {
	ui.leEndOffset->blockSignals(true);
	ui.leEndOffset->setValue(v & 0x3fff);
	ui.leEndOffset->blockSignals(true);
}

#define EL_VAL 512
#define EL_FE 256
#define EL_RD 128
#define EL_WR 64
#define EL_BNK 32
#define EL_SOF 16
#define EL_SAD 8
#define EL_EOF 4
#define EL_EAD 2
#define EL_MSK 1

void xBrkManager::setElements(int mask) {
	ui.brkFetch->setVisible(mask & EL_FE);
	ui.brkRead->setVisible(mask & EL_RD);
	ui.brkWrite->setVisible(mask & EL_WR);
	ui.labFlags->setVisible(mask & (EL_FE | EL_RD | EL_WR));
	ui.brkBank->setVisible(mask & EL_BNK);
	ui.labBank->setVisible(mask & EL_BNK);
	ui.leStartOffset->setVisible(mask & EL_SOF);
	ui.labStartOff->setVisible(mask & EL_SOF);
	ui.brkAdrHex->setVisible(mask & EL_SAD);
	ui.labStartAbs->setVisible(mask & EL_SAD);
	ui.leEndOffset->setVisible(mask & EL_EOF);
	ui.labEndOff->setVisible(mask & EL_EOF);
	ui.brkAdrEnd->setVisible(mask & EL_EAD);
	ui.labEndAbs->setVisible(mask & EL_EAD);
	ui.brkMaskHex->setVisible(mask & EL_MSK);
	ui.labMask->setVisible(mask & EL_MSK);

	ui.labValue->setVisible(mask & EL_VAL);
	ui.labValMask->setVisible(mask & EL_VAL);
	ui.leValue->setVisible(mask & EL_VAL);
	ui.leValMask->setVisible(mask & EL_VAL);
}

void xBrkManager::chaType(int i) {
	int t = ui.brkType->itemData(i).toInt();
	switch (t) {
		case BRK_IRQ:
			setElements(0);
			break;
		case BRK_IOPORT:
			setElements(EL_RD | EL_WR | EL_SAD | EL_MSK);
			break;
		case BRK_CPUADR:
			setElements(EL_FE | EL_RD | EL_WR | EL_SAD | EL_EAD);
			break;
		default:
			setElements(EL_FE | EL_RD | EL_WR | EL_BNK | EL_SOF | EL_SAD | EL_EOF | EL_EAD);
			break;
	}
}

void xBrkManager::edit(xBrkPoint* sbrk) {
	if (sbrk) {
		obrk = *sbrk;
		obrk.off = 0;
	} else {
		obrk.type = BRK_MEMRAM;
		obrk.adr = 0;
		obrk.mask = 0;
		obrk.eadr = 0;
		obrk.off = 0;
		obrk.fetch = 1;
		obrk.read = 0;
		obrk.write = 0;
		obrk.off = 0;
	}
	ui.brkType->setCurrentIndex(ui.brkType->findData(obrk.type));
	ui.brkFetch->setChecked(obrk.fetch);
	ui.brkRead->setChecked(obrk.read);
	ui.brkWrite->setChecked(obrk.write);
	ui.brkAdrHex->setMax(conf.prof.cur->zx->mem->busmask);
	ui.brkAdrEnd->setMax(conf.prof.cur->zx->mem->busmask);
	switch(obrk.type) {
		case BRK_IOPORT:
			ui.brkBank->setValue(0);
			ui.brkAdrHex->setValue(obrk.adr);
			ui.brkMaskHex->setValue(obrk.mask);
			break;
		case BRK_CPUADR:
			ui.brkBank->setValue(0);
			ui.brkAdrHex->setValue(obrk.adr);
			ui.brkAdrEnd->setValue(obrk.eadr);
			ui.brkMaskHex->setText("FFFF");
			break;
		default:
			ui.brkBank->setValue(obrk.adr >> 14);
			ui.brkAdrHex->setValue(obrk.adr);	// &0x3fff ?
			ui.brkAdrEnd->setValue(obrk.eadr);
			ui.brkMaskHex->setText("FFFF");
			break;
	}
	chaType(ui.brkType->currentIndex());
	show();
}

void xBrkManager::confirm() {
	xBrkPoint brk;
	brk.off = 0;
	brk.type = ui.brkType->itemData(ui.brkType->currentIndex()).toInt();
	brk.fetch = ui.brkFetch->isChecked() ? 1 : 0;
	brk.read = ui.brkRead->isChecked() ? 1 : 0;
	brk.write = ui.brkWrite->isChecked() ? 1 : 0;
	switch (brk.type) {
		case BRK_CPUADR:
			brk.adr = ui.brkAdrHex->getValue();
			brk.eadr = ui.brkAdrEnd->getValue();
			break;
		case BRK_IOPORT:
			brk.adr = ui.brkAdrHex->getValue();
			brk.eadr = brk.adr;
			break;
		default:
			//brk.adr = (ui.brkBank->value() << 14) | (ui.brkAdrHex->getValue() & 0x3fff);
			//brk.eadr = brk.adr;
			brk.adr = ui.brkAdrHex->getValue();
			brk.eadr = ui.brkAdrEnd->getValue();
			break;
	}
	brk.mask = ui.brkMaskHex->getValue();
	emit completed(obrk, brk);
	hide();
}

