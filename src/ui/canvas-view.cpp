#include "canvas-view.h"
#include "app.h"

#include <graphics/vec4.h>

#include <QMouseEvent>
#include <QWindow>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

static inline QSize GetPixelSize(QWidget *widget)
{
	return widget->size() * widget->devicePixelRatioF();
}

static bool QTToGSWindow(QWindow *window, gs_window &gswindow)
{
#ifdef _WIN32
	gswindow.hwnd = (HWND)window->winId();
	return true;
#elif defined(__APPLE__)
	gswindow.view = (id)window->winId();
	return true;
#else
	gswindow.id = window->winId();
	gswindow.display = obs_get_nix_platform_display();
	return gswindow.display != nullptr;
#endif
}

CanvasView::CanvasView(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);

	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);

	auto windowVisible = [this](bool visible) {
		if (!visible)
			return;
		if (!display)
			createDisplay();
		else {
			QSize size = GetPixelSize(this);
			obs_display_resize(display, size.width(), size.height());
		}
	};

	connect(windowHandle(), &QWindow::visibleChanged, this, windowVisible);
}

CanvasView::~CanvasView()
{
	destroying = true;
	display = nullptr;
}

void CanvasView::createDisplay()
{
	if (display || destroying)
		return;

	if (!windowHandle() || !windowHandle()->isExposed())
		return;

	QSize size = GetPixelSize(this);

	gs_init_data info = {};
	info.cx = size.width();
	info.cy = size.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;

	if (!QTToGSWindow(windowHandle(), info.window))
		return;

	display = obs_display_create(&info, 0xFF000000);
	obs_display_add_draw_callback(display, drawCallback, this);
}

void CanvasView::getScaleAndOffset(int &x, int &y, float &scale) const
{
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		x = 0;
		y = 0;
		scale = 1.0f;
		return;
	}
	uint32_t baseCX = ovi.base_width;
	uint32_t baseCY = ovi.base_height;
	QSize pixelSize = GetPixelSize(const_cast<CanvasView *>(this));
	int windowCX = pixelSize.width();
	int windowCY = pixelSize.height();

	if (baseCX == 0 || baseCY == 0) {
		x = 0;
		y = 0;
		scale = 1.0f;
		return;
	}

	double windowAspect = double(windowCX) / double(windowCY);
	double baseAspect = double(baseCX) / double(baseCY);

	if (windowAspect > baseAspect) {
		scale = float(windowCY) / float(baseCY);
		int newCX = int(double(windowCY) * baseAspect);
		x = windowCX / 2 - newCX / 2;
		y = 0;
	} else {
		scale = float(windowCX) / float(baseCX);
		int newCY = int(float(windowCX) / baseAspect);
		x = 0;
		y = windowCY / 2 - newCY / 2;
	}
}

void CanvasView::drawCallback(void *data, uint32_t cx, uint32_t cy)
{
	CanvasView *self = static_cast<CanvasView *>(data);
	obs_scene_t *scene = App()->getScene();
	if (!scene)
		return;

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;
	uint32_t baseCX = ovi.base_width;
	uint32_t baseCY = ovi.base_height;
	if (baseCX == 0 || baseCY == 0)
		return;

	int x, y;
	float scale;

	double windowAspect = double(cx) / double(cy);
	double baseAspect = double(baseCX) / double(baseCY);
	int newCX, newCY;

	if (windowAspect > baseAspect) {
		scale = float(cy) / float(baseCY);
		newCX = int(double(cy) * baseAspect);
		newCY = cy;
	} else {
		scale = float(cx) / float(baseCX);
		newCX = cx;
		newCY = int(float(cx) / baseAspect);
	}

	x = cx / 2 - newCX / 2;
	y = cy / 2 - newCY / 2;

	gs_viewport_push();
	gs_projection_push();

	gs_set_viewport(x, y, newCX, newCY);
	gs_ortho(0.0f, (float)baseCX, 0.0f, (float)baseCY, -100.0f, 100.0f);

	obs_source_t *sceneSource = obs_scene_get_source(scene);
	if (sceneSource)
		obs_source_video_render(sceneSource);

	/* Draw selection outline around the selected item */
	obs_sceneitem_t *selected = self->selectedItem;
	if (selected) {
		matrix4 boxTransform;
		obs_sceneitem_get_box_transform(selected, &boxTransform);

		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

		vec4 colorVal;
		vec4_set(&colorVal, 0.0f, 0.8f, 1.0f, 1.0f);
		gs_effect_set_vec4(color, &colorVal);

		gs_matrix_push();
		gs_matrix_mul(&boxTransform);

		while (gs_effect_loop(solid, "Solid")) {
			gs_render_start(true);
			gs_vertex2f(0.0f, 0.0f);
			gs_vertex2f(1.0f, 0.0f);
			gs_vertex2f(1.0f, 1.0f);
			gs_vertex2f(0.0f, 1.0f);
			gs_vertex2f(0.0f, 0.0f);
			gs_vertbuffer_t *vb = gs_render_save();
			gs_load_vertexbuffer(vb);
			gs_draw(GS_LINESTRIP, 0, 0);
			gs_vertexbuffer_destroy(vb);
		}

		/* Draw corner handles */
		float handleSize = HANDLE_RADIUS / scale;
		struct {
			float x, y;
		} corners[] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

		vec4_set(&colorVal, 1.0f, 1.0f, 1.0f, 1.0f);
		gs_effect_set_vec4(color, &colorVal);

		for (auto &c : corners) {
			float hs = handleSize * 0.5f;
			while (gs_effect_loop(solid, "Solid")) {
				gs_render_start(true);
				gs_vertex2f(c.x - hs, c.y - hs);
				gs_vertex2f(c.x + hs, c.y - hs);
				gs_vertex2f(c.x + hs, c.y + hs);
				gs_vertex2f(c.x - hs, c.y + hs);
				gs_vertex2f(c.x - hs, c.y - hs);
				gs_vertbuffer_t *vb = gs_render_save();
				gs_load_vertexbuffer(vb);
				gs_draw(GS_LINESTRIP, 0, 0);
				gs_vertexbuffer_destroy(vb);
			}
		}

		gs_matrix_pop();
	}

	gs_projection_pop();
	gs_viewport_pop();
}

vec2 CanvasView::getMouseScenePos(QMouseEvent *event) const
{
	float dpr = devicePixelRatioF();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	float pixelX = event->position().x() * dpr;
	float pixelY = event->position().y() * dpr;
#else
	float pixelX = event->x() * dpr;
	float pixelY = event->y() * dpr;
#endif

	int offsetX, offsetY;
	float scale;
	getScaleAndOffset(offsetX, offsetY, scale);

	vec2 pos;
	pos.x = (pixelX - offsetX) / scale;
	pos.y = (pixelY - offsetY) / scale;
	return pos;
}

struct ItemAtPosData {
	const vec2 *pos;
	obs_sceneitem_t *result;
};

static bool findItemAtPos(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *data = static_cast<ItemAtPosData *>(param);

	if (!obs_sceneitem_visible(item))
		return true;

	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);

	matrix4 inv;
	if (!matrix4_inv(&inv, &transform))
		return true;

	vec3 pos3;
	vec3_set(&pos3, data->pos->x, data->pos->y, 0.0f);

	vec3 transformed;
	vec3_transform(&transformed, &pos3, &inv);

	if (transformed.x >= 0.0f && transformed.x <= 1.0f && transformed.y >= 0.0f && transformed.y <= 1.0f) {
		data->result = item;
	}

	return true;
}

obs_sceneitem_t *CanvasView::getItemAtPos(const vec2 &pos) const
{
	obs_scene_t *scene = App()->getScene();
	if (!scene)
		return nullptr;

	ItemAtPosData data = {&pos, nullptr};
	obs_scene_enum_items(scene, findItemAtPos, &data);
	return data.result;
}

HandlePosition CanvasView::getHandleAtPos(obs_sceneitem_t *item, const vec2 &pos) const
{
	if (!item)
		return HandlePosition::None;

	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);

	int offsetX, offsetY;
	float scale;
	getScaleAndOffset(offsetX, offsetY, scale);
	float handleScreenRadius = HANDLE_RADIUS / scale;

	struct {
		HandlePosition handle;
		float x, y;
	} corners[] = {
		{HandlePosition::TopLeft, 0, 0},
		{HandlePosition::TopRight, 1, 0},
		{HandlePosition::BottomLeft, 0, 1},
		{HandlePosition::BottomRight, 1, 1},
	};

	for (auto &c : corners) {
		vec3 corner;
		vec3_set(&corner, c.x, c.y, 0.0f);

		vec3 screenPos;
		vec3_transform(&screenPos, &corner, &transform);

		float dx = pos.x - screenPos.x;
		float dy = pos.y - screenPos.y;
		float dist = sqrtf(dx * dx + dy * dy);

		if (dist <= handleScreenRadius)
			return c.handle;
	}

	return HandlePosition::None;
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}

	vec2 scenePos = getMouseScenePos(event);

	if (selectedItem) {
		HandlePosition handle = getHandleAtPos(selectedItem, scenePos);
		if (handle != HandlePosition::None) {
			activeHandle = handle;
			dragging = true;
			dragStartMouse = scenePos;

			vec2 itemPos;
			obs_sceneitem_get_pos(selectedItem, &itemPos);
			dragStartItemPos = itemPos;

			vec2 itemScale;
			obs_sceneitem_get_scale(selectedItem, &itemScale);
			dragStartItemScale = itemScale;
			return;
		}
	}

	obs_sceneitem_t *item = getItemAtPos(scenePos);
	selectedItem = item;

	if (item) {
		dragging = true;
		activeHandle = HandlePosition::None;
		dragStartMouse = scenePos;

		vec2 itemPos;
		obs_sceneitem_get_pos(item, &itemPos);
		dragStartItemPos = itemPos;
	}

	update();
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		dragging = false;
		activeHandle = HandlePosition::None;
	}

	QWidget::mouseReleaseEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging || !selectedItem) {
		QWidget::mouseMoveEvent(event);
		return;
	}

	vec2 scenePos = getMouseScenePos(event);
	float dx = scenePos.x - dragStartMouse.x;
	float dy = scenePos.y - dragStartMouse.y;

	if (activeHandle == HandlePosition::None) {
		vec2 newPos;
		newPos.x = dragStartItemPos.x + dx;
		newPos.y = dragStartItemPos.y + dy;
		obs_sceneitem_set_pos(selectedItem, &newPos);
	} else {
		obs_source_t *source = obs_sceneitem_get_source(selectedItem);
		uint32_t sourceW = obs_source_get_width(source);
		uint32_t sourceH = obs_source_get_height(source);

		if (sourceW == 0 || sourceH == 0)
			return;

		float origW = float(sourceW) * dragStartItemScale.x;
		float origH = float(sourceH) * dragStartItemScale.y;

		float newW = origW;
		float newH = origH;
		vec2 newPos = dragStartItemPos;

		switch (activeHandle) {
		case HandlePosition::BottomRight:
			newW = origW + dx;
			newH = origH + dy;
			break;
		case HandlePosition::BottomLeft:
			newW = origW - dx;
			newH = origH + dy;
			newPos.x = dragStartItemPos.x + dx;
			break;
		case HandlePosition::TopRight:
			newW = origW + dx;
			newH = origH - dy;
			newPos.y = dragStartItemPos.y + dy;
			break;
		case HandlePosition::TopLeft:
			newW = origW - dx;
			newH = origH - dy;
			newPos.x = dragStartItemPos.x + dx;
			newPos.y = dragStartItemPos.y + dy;
			break;
		default:
			break;
		}

		if (newW < 32.0f)
			newW = 32.0f;
		if (newH < 32.0f)
			newH = 32.0f;

		vec2 newScale;
		newScale.x = newW / float(sourceW);
		newScale.y = newH / float(sourceH);

		obs_sceneitem_set_pos(selectedItem, &newPos);
		obs_sceneitem_set_scale(selectedItem, &newScale);
	}
}

void CanvasView::paintEvent(QPaintEvent *event)
{
	createDisplay();
	QWidget::paintEvent(event);
}

void CanvasView::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	createDisplay();

	if (isVisible() && display) {
		QSize size = GetPixelSize(this);
		obs_display_resize(display, size.width(), size.height());
	}
}

void CanvasView::moveEvent(QMoveEvent *event)
{
	QWidget::moveEvent(event);

	if (display)
		obs_display_update_color_space(display);
}
