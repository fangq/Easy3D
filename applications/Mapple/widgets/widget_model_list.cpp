#include <applications/Mapple/widgets/widget_model_list.h>
#include <applications/Mapple/main_window.h>
#include <applications/Mapple/paint_canvas.h>

#include <easy3d/core/graph.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/manifold_builder.h>
#include <easy3d/util/file_system.h>
#include <easy3d/fileio/resources.h>

#include <QMenu>
#include <QColorDialog>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QMouseEvent>


using namespace easy3d;


class ModelItem : public QTreeWidgetItem {
public:
    ModelItem(QTreeWidget *parent) : QTreeWidgetItem(parent) {
        for (int i = 0; i < parent->columnCount(); ++i)
            QTreeWidgetItem::setTextAlignment(i, Qt::AlignLeft);

        selected_color_ = Qt::yellow;
        active_color_ = QColor(255, 177, 255);
    }

    ~ModelItem() {}

    Model *model() { return model_; }

    void setModel(Model *model) { model_ = model; }

    void setIcon(int column) {
        if (dynamic_cast<SurfaceMesh *>(model())) {
            static QIcon iconMesh(QString::fromStdString(resource::directory() + "/icons/mesh.png"));
            QTreeWidgetItem::setIcon(column, iconMesh);
        } else if (dynamic_cast<PointCloud *>(model())) {
            static QIcon iconPointCloud(QString::fromStdString(resource::directory() + "/icons/point_cloud.png"));
            QTreeWidgetItem::setIcon(column, iconPointCloud);
        } else if (dynamic_cast<Graph *>(model())) {
            static QIcon iconGraph(QString::fromStdString(resource::directory() + "/icons/graph.png"));
            QTreeWidgetItem::setIcon(column, iconGraph);
        }
//        else if (dynamic_cast<CGraph*>(model())) {
//            static QIcon iconTetrahedra(QString::fromStdString(resource::directory() + "/icons/tetrahedra.png"));
//            QTreeWidgetItem::setIcon(column, iconTetrahedra);
//        }
    }

    void setVisibilityIcon(int column, bool visible) {
        static QIcon iconShow(QString::fromStdString(resource::directory() + "/icons/show.png"));
        static QIcon iconHide(QString::fromStdString(resource::directory() + "/icons/hide.png"));

        if (visible)
            QTreeWidgetItem::setIcon(column, iconShow);
        else {
            QTreeWidgetItem::setIcon(column, iconHide);
        }
    }

    void setStatus(bool is_selected, bool is_active) {
        if (is_active)
            setColor(active_color_);
        else if (is_selected)
            setColor(selected_color_);
        else
            resetColor();

        model_->set_selected(is_selected);
        QTreeWidgetItem::setSelected(is_selected);
    }


private:
    void setColor(const QColor &c) {    // for all columns
        for (int i = 0; i < columnCount(); ++i) {
            QTreeWidgetItem::setBackground(i, c);
        }
    }

    void resetColor() {    // for all columns
        for (int i = 0; i < columnCount(); ++i) {
            QTreeWidgetItem::setBackground(i, QBrush());
        }
    }

private:
    Model *model_;

    QColor selected_color_;
    QColor active_color_;
};


WidgetModelList::WidgetModelList(QWidget *parent)
        : QTreeWidget(parent), mainWindow_(nullptr), popupMenu_(nullptr), auto_focus_(false), selected_only_(false)
{
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, SIGNAL(itemClicked(QTreeWidgetItem * , int)), this, SLOT(modelItemClicked(QTreeWidgetItem * , int)));
    connect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem * , int)), this,
            SLOT(modelItemDoubleClicked(QTreeWidgetItem * , int)));
    connect(this, SIGNAL(currentItemChanged(QTreeWidgetItem * , QTreeWidgetItem * )), this,
            SLOT(currentModelItemChanged(QTreeWidgetItem * , QTreeWidgetItem * )));
    connect(this, SIGNAL(itemSelectionChanged()), this, SLOT(modelItemSelectionChanged()));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
}


WidgetModelList::~WidgetModelList() {
}


void WidgetModelList::init(MainWindow *w) {
    mainWindow_ = w;

    QStringList headerLabels;
    headerLabels << "Index" << "Type" << "Show" << "Name";
    setHeaderLabels(headerLabels);
    for (int i = 0; i < columnCount(); ++i)
        headerItem()->setTextAlignment(i, Qt::AlignLeft);

    header()->setDefaultSectionSize(50);

//    setColumnWidth(0, 50);
    setIndentation(10);

    setSelectionMode(QAbstractItemView::ExtendedSelection);
}


PaintCanvas *WidgetModelList::canvas() {
    return mainWindow_->viewer();
}


void WidgetModelList::prepareContextMenu(QMenu *menu) {
    menu->clear();  // I want to customize the menu list

    int num_selected = 0;
    int num_selected_meshes = 0;
    int num_unselected = 0;
    int num_invisible = 0;
    int num_visible_in_selected = 0;
    int num_invisible_in_selected = 0;

    int total = topLevelItemCount();
    for (int i = 0; i < total; ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        Model *model = dynamic_cast<ModelItem *>(item)->model();

        if (item->isSelected()) {
            ++num_selected;
            if (dynamic_cast<SurfaceMesh *>(model))
                ++num_selected_meshes;
        } else
            ++num_unselected;

        if (!model->is_visible()) {
            ++num_invisible;
            if (item->isSelected())
                ++num_invisible_in_selected;
        } else {
            if (item->isSelected())
                ++num_visible_in_selected;
        }
    }

    QAction *actionInvertSelection = menu->addAction("Invert Selection");
    connect(actionInvertSelection, SIGNAL(triggered()), this, SLOT(invertSelection()));
    menu->addSeparator();

    // hide / show
    if (num_invisible_in_selected > 0) {
        QAction *actionShowSelected = menu->addAction("Show");
        connect(actionShowSelected, SIGNAL(triggered()), this, SLOT(showSelected()));
    }

    if (num_visible_in_selected > 0) {
        QAction *actionHideSelected = menu->addAction("Hide");
        connect(actionHideSelected, SIGNAL(triggered()), this, SLOT(hideSelected()));
    }

    QAction *actionInvertShowHide = menu->addAction("Invert Show/Hide");
    connect(actionInvertShowHide, SIGNAL(triggered()), this, SLOT(invertShowHide()));

    if (num_invisible > 0) {
        QAction *actionShowAll = menu->addAction("Show All");
        connect(actionShowAll, SIGNAL(triggered()), this, SLOT(showAll()));
    }

    menu->addSeparator();

    // manipulation
    if (num_selected == 1) {
        QAction *actionDuplicateCurrent = menu->addAction(
                QIcon(QString::fromStdString(resource::directory() + "/icons/duplicate.png")), "Duplicate");
        connect(actionDuplicateCurrent, SIGNAL(triggered()), this, SLOT(duplicateCurrent()));
    } else if (num_selected > 1) {
        QAction *actionMergeSelected = menu->addAction(
                QIcon(QString::fromStdString(resource::directory() + "/icons/merge.png")), "Merge");
        connect(actionMergeSelected, SIGNAL(triggered()), this, SLOT(mergeSelected()));
    }

    if (num_selected_meshes > 0) {
        QAction *actionDecomposeSelected = menu->addAction(
                QIcon(QString::fromStdString(resource::directory() + "/icons/decompose.png")), "Decompose");
        connect(actionDecomposeSelected, SIGNAL(triggered()), this, SLOT(decomposeSelected()));
    }

    if (num_selected > 0) {
        menu->addSeparator();
        QAction *actionDeleteSelected = menu->addAction(
                QIcon(QString::fromStdString(resource::directory() + "/icons/delete.png")), "Delete");
        connect(actionDeleteSelected, SIGNAL(triggered()), this, SLOT(deleteSelected()));
    }
}


void WidgetModelList::showContextMenu(const QPoint &pos) {
    if (!popupMenu_)
        popupMenu_ = new QMenu(this);

    prepareContextMenu(popupMenu_);

    // Liangliang: the header height was not counted by default?
    //popupMenu_->popup(mapToGlobal(pos));
    QPoint p = mapToGlobal(pos);
    popupMenu_->popup(QPoint(p.x(), p.y() + header()->height()));
}


void WidgetModelList::updateModelList() {
    disconnect(this, SIGNAL(itemClicked(QTreeWidgetItem * , int)), this,
               SLOT(modelItemClicked(QTreeWidgetItem * , int)));
    disconnect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem * , int)), this,
               SLOT(modelItemDoubleClicked(QTreeWidgetItem * , int)));
    disconnect(this, SIGNAL(currentItemChanged(QTreeWidgetItem * , QTreeWidgetItem * )), this,
               SLOT(currentModelItemChanged(QTreeWidgetItem * , QTreeWidgetItem * )));
    disconnect(this, SIGNAL(itemSelectionChanged()), this, SLOT(modelItemSelectionChanged()));

    Model *active_model = canvas()->currentModel();

    const std::vector<Model *> &models = canvas()->models();
    for (unsigned int i = 0; i < models.size(); ++i) {
        Model *model = models[i];
        const std::string &name = file_system::base_name(model->name());
        ModelItem *item = dynamic_cast<ModelItem *>(this->topLevelItem(i));
        if (!item) {
            item = new ModelItem(this);
            addTopLevelItem(item);
        }
        item->setModel(model);

        item->setData(0, Qt::DisplayRole, i + 1);
        item->setIcon(1);
        item->setVisibilityIcon(2, model->is_visible());

        item->setData(3, Qt::DisplayRole, QString::fromStdString(name));
		item->setStatus(model->is_selected(), model == active_model);

        if (model == active_model)
            setCurrentItem(item);
    }

    // remove redundant items
    if (topLevelItemCount() > models.size()) {
        int delta = int(topLevelItemCount() - models.size());
        for (int i = 0; i < delta; ++i) {
            int idx = topLevelItemCount() - 1;
            if (idx >= 0) {
                QTreeWidgetItem *item = takeTopLevelItem(idx);
                delete item;
            } else
                LOG(FATAL) << "should not reach here";
        }
    }

    assert(topLevelItemCount() == models.size());

    update();

    connect(this, SIGNAL(itemClicked(QTreeWidgetItem * , int)), this, SLOT(modelItemClicked(QTreeWidgetItem * , int)));
    connect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem * , int)), this,
            SLOT(modelItemDoubleClicked(QTreeWidgetItem * , int)));
    connect(this, SIGNAL(currentItemChanged(QTreeWidgetItem * , QTreeWidgetItem * )), this,
            SLOT(currentModelItemChanged(QTreeWidgetItem * , QTreeWidgetItem * )));
    connect(this, SIGNAL(itemSelectionChanged()), this, SLOT(modelItemSelectionChanged()));

    if (canvas()->currentModel()) {
        const std::string &name = canvas()->currentModel()->name();
        if (!name.empty()) {
            mainWindow_->setCurrentFile(QString::fromStdString(name));
        }
    } else
        mainWindow_->setCurrentFile(QString());
}


void WidgetModelList::duplicateCurrent() {
    if (canvas()->currentModel()) {
        Model* model = canvas()->currentModel();

        Model* copy = nullptr;
        if (dynamic_cast<SurfaceMesh*>(model)) {
            copy = new SurfaceMesh(*dynamic_cast<SurfaceMesh*>(model));
        }
        else if (dynamic_cast<PointCloud*>(model)) {
            copy = new PointCloud(*dynamic_cast<PointCloud*>(model));
        }
        else if (dynamic_cast<Graph*>(model)) {
            copy = new Graph(*dynamic_cast<Graph*>(model));
        }
//        else if (dynamic_cast<Tetrahedra*>(model)) {
//            copy = new Tetrahedra(*dynamic_cast<Tetrahedra*>(model));
//        }

        if (copy) {
            const std::string name = file_system::parent_directory(model->name()) + "/" + file_system::base_name(model->name()) + "_copy";
            copy->set_name(name);

            canvas()->makeCurrent();
            canvas()->addModel(copy, true);
            canvas()->doneCurrent();

            addModel(copy, true, false);
        }
    }
}


void WidgetModelList::hideOtherModels(Model *cur) {
    const std::vector<Model *> &models = canvas()->models();
    for (int i = 0; i < models.size(); ++i) {
        Model *model = models[i];
        if (selected_only_ && model != cur)
            model->set_visible(false);
        else
            model->set_visible(true);
    }
}


void WidgetModelList::showAll() {
//	if (selected_only_) {
//        mainWindow_->actionShowSelectedOnly->setChecked(false);
//    }
//	else {
//		const std::vector<Model*>& models = canvas()->models();
//		for (int i = 0; i < models.size(); ++i) {
//			models[i]->set_visible(true);
//		}
//		canvas()->update();
//		updateModelList();
//	}
}


void WidgetModelList::showSelected() {
    if (selectedItems().empty())
        return;

	const QList<QTreeWidgetItem*>& items = selectedItems();
	for (int i = 0; i < items.size(); ++i) {
		ModelItem* item = dynamic_cast<ModelItem*>(items[i]);
		Model* model = item->model();
		model->set_visible(true);
	}

    canvas()->update();
}


void WidgetModelList::hideSelected() {
    if (selectedItems().empty())
        return;

	const QList<QTreeWidgetItem*>& items = selectedItems();
	for (int i = 0; i < items.size(); ++i) {
		ModelItem* item = dynamic_cast<ModelItem*>(items[i]);
		Model* model = item->model();
		model->set_visible(false);
	}

    canvas()->update();
}


void WidgetModelList::invertShowHide() {
//	int num = topLevelItemCount();
//
//	if (selected_only_) {
//		std::vector<unsigned char> visible(num);
//		for (int i = 0; i < num; ++i) {
//			QTreeWidgetItem* item = topLevelItem(i);
//			ModelItem* modelItem = dynamic_cast<ModelItem*>(item);
//			visible[i] = (!modelItem->currentModel()->is_visible());
//		}
//		mainWindow_->actionShowSelectedOnly->setChecked(false);
//		for (int i = 0; i < num; ++i) {
//			QTreeWidgetItem* item = topLevelItem(i);
//			ModelItem* modelItem = dynamic_cast<ModelItem*>(item);
//			modelItem->currentModel()->set_visible(visible[i]);
//		}
//	}
//	else {
//		for (int i = 0; i < num; ++i) {
//			QTreeWidgetItem* item = topLevelItem(i);
//			ModelItem* modelItem = dynamic_cast<ModelItem*>(item);
//			Model* model = modelItem->currentModel();
//			model->set_visible(!model->is_visible());;
//		}
//	}
//
    canvas()->update();

}


void WidgetModelList::decomposeSelected() {
    const QList<QTreeWidgetItem *> &items = selectedItems();
    for (int i = 0; i < items.size(); ++i) {
        ModelItem *item = dynamic_cast<ModelItem *>(items[i]);
        Model *model = item->model();
        decomposeModel(model);
    }
}


void WidgetModelList::invertSelection() {
    disconnect(this, SIGNAL(itemSelectionChanged()), this, SLOT(modelItemSelectionChanged()));
    int num = topLevelItemCount();
    for (int i = 0; i < num; ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        item->setSelected(!item->isSelected());
    }
    connect(this, SIGNAL(itemSelectionChanged()), this, SLOT(modelItemSelectionChanged()));

    emit itemSelectionChanged();
}


void WidgetModelList::mergeSelected() {
//	std::vector<Model*>	models;
//	const QList<QTreeWidgetItem*>& items = selectedItems();
//	for (int i = 0; i < items.size(); ++i) {
//		ModelItem* item = dynamic_cast<ModelItem*>(items[i]);
//		models.push_back(item->currentModel());
//	}
//
//	mergeModels(models);
}


void WidgetModelList::deleteSelected() {
//	std::vector<Model*>	models;
//	const QList<QTreeWidgetItem*>& items = selectedItems();
//	for (int i = 0; i < items.size(); ++i) {
//		ModelItem* item = dynamic_cast<ModelItem*>(items[i]);
//		models.push_back(item->currentModel());
//	}
//
//	deleteModels(models);
}


void WidgetModelList::currentModelItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    if (current == previous)
        return;

	ModelItem* current_item = dynamic_cast<ModelItem*>(current);
	ModelItem* previous_item = dynamic_cast<ModelItem*>(previous);
	if (!current_item)
		return;

    current_item->setStatus(current_item->model()->is_selected(), true);
    if (previous)
        previous_item->setStatus(previous_item->model()->is_selected(), false);

	Model* model = current_item->model();
	canvas()->setCurrentModel(model);

	if (selected_only_) {
		hideOtherModels(model);
		updateModelList();
	}

	if (auto_focus_)
		canvas()->fitScreen(model);
	else
		canvas()->update();

    mainWindow_->onCurrentModelChanged();
}


void WidgetModelList::modelItemSelectionChanged() {
    Model *active_model = canvas()->currentModel();

    int num = topLevelItemCount();
    for (int i = 0; i < num; ++i) {
        ModelItem *item = dynamic_cast<ModelItem *>(topLevelItem(i));
        item->setStatus(item->isSelected(), item->model() == active_model);
    }

//	canvas()->configureManipulation();
    canvas()->update();
}


void WidgetModelList::mousePressEvent(QMouseEvent *e) {
    if (selected_only_) {
        ModelItem *curr_item = dynamic_cast<ModelItem *>(currentItem());
        if (curr_item && itemAt(e->pos()) == nullptr) {
            curr_item->setStatus(false, true);
        }
    }

    QTreeWidget::mousePressEvent(e);
}

void WidgetModelList::mouseReleaseEvent(QMouseEvent *e) {
    QTreeWidget::mouseReleaseEvent(e);
}


void WidgetModelList::modelItemClicked(QTreeWidgetItem *current, int column) {
    ModelItem *current_item = dynamic_cast<ModelItem *>(current);
    if (!current_item)
        return;

    Model *model = current_item->model();

    if (column == 2 && !selected_only_) {
        bool visible = !model->is_visible();
        current_item->setVisibilityIcon(2, visible);
        model->set_visible(visible);
    }

    canvas()->update();
}


void WidgetModelList::modelItemDoubleClicked(QTreeWidgetItem *current, int column) {
//	ModelItem* item = dynamic_cast<ModelItem*>(current);
//	if (!item)
//		return;
//
//	Model* model = item->model();
//	if (column == 3) {
//	    // if you want to change something here
//	}
//	canvas()->update();
//	mainWindow_->onCurrentModelChanged();
}


void WidgetModelList::setAutoFocus(bool b) {
    auto_focus_ = b;
    if (auto_focus_)
        canvas()->fitScreen();
}


void WidgetModelList::setSelectedOnly(bool b) {
	selected_only_ = b;
	if (selected_only_)
		setSelectionMode(QAbstractItemView::SingleSelection);
	else
		setSelectionMode(QAbstractItemView::ExtendedSelection);

	ModelItem* item = dynamic_cast<ModelItem*>(currentItem());
	if (!item)
		return;

	Model* active_model = item->model();
	if (selected_only_)
		hideOtherModels(active_model);
	else {
		const std::vector<Model*>& models = canvas()->models();
		for (int i = 0; i < models.size(); ++i) {
			models[i]->set_visible(true);
		}
	}

    canvas()->update();

	mainWindow_->onCurrentModelChanged();
}


void WidgetModelList::addModel(Model *model, bool make_current, bool fit) {
    const auto &models = canvas()->models();
    if (!model || models.empty())
        return;

    Model *current_model = canvas()->currentModel();
    if (selected_only_)
        hideOtherModels(current_model);

    if (fit)
        canvas()->fitScreen(current_model);
    else
        canvas()->update();

    if (make_current)
        mainWindow_->enableCameraManipulation();
}


void WidgetModelList::deleteModel(Model *model, bool fit) {
//	ModelManager* manager = canvas()->modelManager();
//	if (!manager)
//		return;
//
//	manager->delete_model(model);
//	Model* active_model = manager->current_model();
//	if (selected_only_)
//		hideOtherModels(active_model);
//
//	if (fit)
//		canvas()->fitScreen();
//	else
//		canvas()->update();
//	canvas()->configureManipulation();
//

//	mainWindow_->onCurrentModelChanged();
//	
//	
}


void WidgetModelList::addModels(const std::vector<Model *> &models) {
//	if (models.empty())
//		return;
//
//	ModelManager* manager = canvas()->modelManager();
//	if (!manager)
//		return;
//
//	for (std::size_t i = 0; i < models.size(); ++i) {
//		Model* model = models[i];
//		model->set_canvas(canvas());
//		bool make_another_current = (i == (models.size() - 1));
//		canvas()->modelManager()->add_model(model, make_another_current);
//	}
//
//	Model* active_model = manager->current_model();
//	if (selected_only_)
//		hideOtherModels(active_model);
//
    canvas()->update();
//

//	mainWindow_->onCurrentModelChanged();
//	
//	
}


// delete all
void WidgetModelList::clear() {
//	ModelManager* manager = canvas()->modelManager();
//	if (!manager)
//		return;
//
//	manager->clear();
//

//	mainWindow_->onCurrentModelChanged();
//	
//	
}


void WidgetModelList::deleteModels(const std::vector<Model *> &models) {
//	ModelManager* manager = canvas()->modelManager();
//	if (!manager)
//		return;
//
//	for (std::size_t i = 0; i < models.size(); ++i) { //
//		Model* model = models[i];
//		manager->delete_model(model);
//	}
//
//	Model* current_model = manager->current_model();
//	if (selected_only_)
//		hideOtherModels(current_model);
//
//	if (auto_focus_)
//		canvas()->fitScreen();
//	else
//		canvas()->update();
//	canvas()->configureManipulation();
//

//	mainWindow_->onCurrentModelChanged();
//	
//	
}


void WidgetModelList::mergeModels(const std::vector<Model *> &models) {
    std::vector<SurfaceMesh *> meshes;
    std::vector<PointCloud *> clouds;

    std::vector<Model *> selectedModels;
    for (std::size_t i = 0; i < models.size(); ++i) {
        Model *m = models[i];
        if (dynamic_cast<SurfaceMesh *>(m)) {
            SurfaceMesh *mesh = dynamic_cast<SurfaceMesh *>(m);
            meshes.push_back(mesh);
        } else if (dynamic_cast<PointCloud *>(m)) {
            PointCloud *pset = dynamic_cast<PointCloud *>(m);
            clouds.push_back(pset);
        }
    }

    std::vector<Model *> to_delete;

//	if (meshes.size() > 1) {
//		SurfaceMesh* to = meshes[0];
//		ProgressLogger logger(meshes.size() - 1, "Merge meshes");
//		for (int i = 1; i < meshes.size(); ++i) {
//			if (logger.is_canceled())
//				break;
//
//			SurfaceMesh* from = meshes[i];
//			Geom::merge(to, from);
//
//			to_delete.push_back(from);
//			logger.next();
//		}
//
//		to->set_name("merged_mesh");
//		to->notify_vertex_change();
//		to->reset_manipulated_frame();
//	}
//
//	if (clouds.size() > 1) {
//		PointCloud* to = clouds[0];
//		ProgressLogger logger(clouds.size() - 1, "Merge point sets");
//		for (int i = 1; i < clouds.size(); ++i) {
//			if (logger.is_canceled())
//				break;
//
//			PointCloud* from = clouds[i];
//			Geom::merge(to, from);
//
//			to_delete.push_back(from);
//			logger.next();
//		}
//
//		to->set_name("merged_point_set");
//		to->notify_vertex_change();
//		to->reset_manipulated_frame();
//	}
//
//	for (unsigned int i = 0; i < to_delete.size(); ++i) {
//		Model* model = to_delete[i];
//		canvas()->modelManager()->delete_model(model);
//	}
//
//	// update display and ui
//	if (meshes.size() > 1 || clouds.size() > 1) {
//		canvas()->configureManipulation();
//		canvas()->update();
//		updateModelList();
//		mainWindow_->onCurrentModelChanged();
//		
//		
//	}
}


void WidgetModelList::decomposeModel(Model *model) {
    SurfaceMesh *mesh = dynamic_cast<SurfaceMesh *>(model);
    if (!mesh)
        return;
//
//	MeshComponentsExtractor extractor;
//	const MeshComponentList& components = extractor.extract_components(mesh);
//	if (components.size() == 1) {
//		Logger::warn(title()) << "model has only one component" << std::endl;
//		return;
//	}
//
//	bool copy_attributes = true;
//	std::string base_name = file_system::dir_name(mesh->name()) + "/" + file_system::base_name(mesh->name()) + "_part_";
//
//	MeshVertexAttribute<int>	vertex_id(mesh);
//	MeshTexVertexAttribute<int>	tex_vertex_id(mesh);
//	MeshCopier copier;
//	copier.set_copy_all_attributes(copy_attributes);
//
//	ProgressLogger logger(components.size(), "dispatch components");
//	for (unsigned int i = 0; i < components.size(); i++) {
//		logger.notify(i);
//		if (logger.is_canceled()) {
//			break;
//		}
//
//		std::string name = base_name + std::to_string(i + 1);
//
//		SurfaceMesh* new_mesh = new SurfaceMesh;
//		mpl_debug_assert(new_mesh != nullptr);
//		new_mesh->set_name(name);
//
//		MeshBuilder builder(new_mesh);
//		int cur_vertex_id = 0;
//		int cur_tex_vertex_id = 0;
//		builder.begin_surface();
//		copier.copy(builder, components[i], vertex_id, tex_vertex_id, cur_vertex_id, cur_tex_vertex_id);
//		builder.end_surface();
//		// 		new_mesh->compute_facet_normals();
//		// 		new_mesh->compute_vertex_normals();
//
//		new_mesh->set_canvas(canvas());
//		bool activate = (i == components.size() - 1); // make the last current
//		canvas()->modelManager()->add_model(new_mesh, activate);
//	}
//
//	vertex_id.unbind();
//	tex_vertex_id.unbind();
//
//	// delete the original model
//	deleteModel(mesh, false);
//
//	Logger::out(title()) << "decomposed into " << components.size() << " parts" << std::endl;
}
