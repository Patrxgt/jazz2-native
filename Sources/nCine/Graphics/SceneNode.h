#pragma once

#include "../Base/Object.h"
#include "../Primitives/Vector2.h"
#include "../Primitives/Matrix4x4.h"
#include "../Primitives/Color.h"
#include "../Primitives/Colorf.h"
#include "../Base/BitSet.h"
#include "../CommonConstants.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class RenderQueue;
	class Viewport;

	/// Base class for the transformation nodes hierarchy
	class SceneNode : public Object
	{
	public:
		enum class VisitOrderState
		{
			Disabled,
			Enabled,
			SameAsParent
		};

		/** @{ @name Constants */

		/// Minimum amount of rotation to trigger a sine and cosine calculation
		static constexpr float MinRotation = 0.5f * fDegToRad;

		/** @} */

		/// Constructor for a node with a parent and a specified relative position
		SceneNode(SceneNode* parent, float x, float y);
		/// Constructor for a node with a parent and a specified relative position as a vector
		SceneNode(SceneNode* parent, Vector2f position);
		/// Constructor for a node with a parent and positioned in the relative origin
		explicit SceneNode(SceneNode* parent);
		/// Constructor for a node with no parent and positioned in the origin
		SceneNode();
		/// Destructor will delete every child node
		~SceneNode() override;

		SceneNode& operator=(const SceneNode&) = delete;
		SceneNode(SceneNode&& other) noexcept;
		SceneNode& operator=(SceneNode&& other) noexcept;

		/// Returns a copy of this object
		inline SceneNode clone() const {
			return SceneNode(*this);
		}

		inline static ObjectType sType() {
			return ObjectType::SceneNode;
		}

		/// Returns the parent as a constant node, if there is any
		inline const SceneNode* parent() const {
			return parent_;
		}
		/// Returns the parent node, if there is any
		inline SceneNode* parent() {
			return parent_;
		}
		/// Sets the parent node
		bool setParent(SceneNode* parentNode);
		/// Returns the array of child nodes
		inline const SmallVectorImpl<SceneNode*>& children() {
			return children_;
		}
		/// Returns an array of constant child nodes
		const SmallVectorImpl<const SceneNode*>& children() const;
		/// Adds a node as a child of this one
		bool addChildNode(SceneNode* childNode);
		/// Removes a child of this node, without reparenting nephews
		bool removeChildNode(SceneNode* childNode);
		/// Removes the child at the specified index, without reparenting nephews
		bool removeChildNodeAt(std::uint32_t index);
		/// Removes all children, without reparenting nephews
		bool removeAllChildrenNodes();
		/// Removes a child of this node reparenting nephews as children
		bool unlinkChildNode(SceneNode* childNode);

		/// Returns the child order index of this node or zero if it does not have a parent
		std::uint32_t childOrderIndex() const;
		/// Swaps two children at the specified indices
		bool swapChildrenNodes(std::uint32_t firstIndex, std::uint32_t secondIndex);
		/// Brings this node one node forward in the parent's list of children
		bool swapNodeForward();
		/// Brings this node one node back in the parent's list of children
		bool swapNodeBack();

		/// Returns `true` if the node visit order is used together with the layer
		inline enum VisitOrderState visitOrderState() const {
			return visitOrderState_;
		}
		/// Enables the use of the node visit order together with the layer
		inline void setVisitOrderState(enum VisitOrderState visitOrderState) {
			visitOrderState_ = visitOrderState;
		}
		/// Returns the visit drawing order of the node
		inline std::uint16_t visitOrderIndex() const {
			return visitOrderIndex_;
		}

		/** @brief Called every frame to update the object state */
		virtual void OnUpdate(float timeMult);
		/// Draws the node and visits its children
		virtual void OnVisit(RenderQueue& renderQueue, std::uint32_t& visitOrderIndex);
		/** @brief Called when the object needs to be drawn */
		virtual bool OnDraw(RenderQueue& renderQueue) {
			return false;
		}

		/// Returns `true` if the node is updating
		inline bool isUpdateEnabled() const {
			return updateEnabled_;
		}
		/// Enables or disables node updating
		inline void setUpdateEnabled(bool updateEnabled) {
			updateEnabled_ = updateEnabled;
		}
		/// Returns `true` if the node is drawing
		inline bool isDrawEnabled() const {
			return drawEnabled_;
		}
		/// Enables or disables node drawing
		inline void setDrawEnabled(bool drawEnabled) {
			drawEnabled_ = drawEnabled;
		}
		/// Returns `true` if the node is both updating and drawing
		inline bool isEnabled() const {
			return (updateEnabled_ == true && drawEnabled_ == true);
		}
		/// Enables or disables both node updating and drawing
		void setEnabled(bool isEnabled);

		/// Returns node position relative to its parent
		inline Vector2f position() const {
			return position_;
		}
		/// Returns absolute node position
		inline Vector2f absPosition() const {
			return absPosition_;
		}
		/// Sets the node position through two coordinates
		void setPosition(float x, float y);
		/// Sets the node position through a vector
		void setPosition(Vector2f position);
		/// Sets the X coordinate of the node position
		void setPositionX(float x);
		/// Sets the Y coordinate of the node position
		void setPositionY(float y);
		/// Moves the node based on two offsets
		void move(float x, float y);
		/// Adds a move vector to the node current position
		void move(Vector2f position);
		/// Moves the node by an offset on the X axis
		void moveX(float x);
		/// Moves the node by an offset on the Y axis
		void moveY(float y);

		/// Gets the transformation anchor point in pixels
		inline Vector2f absAnchorPoint() const {
			return anchorPoint_;
		}
		/// Sets the transformation anchor point in pixels
		void setAbsAnchorPoint(float x, float y);
		/// Sets the transformation anchor point in pixels with a `Vector2f`
		void setAbsAnchorPoint(Vector2f point);

		/// Gets the node scale factors
		inline const Vector2f& scale() const {
			return scaleFactor_;
		}
		/// Gets the node absolute scale factors
		inline const Vector2f& absScale() const {
			return absScaleFactor_;
		}
		/// Scales the node size both horizontally and vertically
		void setScale(float scaleFactor);
		/// Scales the node size both horizontally and vertically
		void setScale(float scaleFactorX, float scaleFactorY);
		/// Scales the node size both horizontally and vertically with a `Vector2f`
		void setScale(Vector2f scaleFactor);

		/// Gets the node rotation in radians
		inline float rotation() const {
			return rotation_;
		}
		/// Gets the node absolute rotation in radians
		inline float absRotation() const {
			return absRotation_;
		}
		/// Sets the node rotation in radians
		void setRotation(float rotation);

		/// Gets the node color
		inline Colorf color() const {
			return color_;
		}
		/// Gets the node absolute color
		inline Colorf absColor() const {
			return absColor_;
		}
		/// Sets the node color through a `Color` object
		void setColor(const Color& color);
		/// Sets the node color through a `Colorf` object
		void setColor(const Colorf& color);
		/// Gets the node alpha
		inline float alpha() const {
			return color_.A;
		}
		/// Gets the node absolute alpha
		inline float absAlpha() const {
			return absColor_.A;
		}
		/// Sets the node alpha through an unsigned char component
		void setAlpha(std::uint8_t alpha);
		/// Sets the node alpha through a float component
		void setAlphaF(float alpha);

		/// Gets the node rendering layer
		inline std::uint16_t layer() const {
			return layer_;
		}
		/// Gets the node absolute rendering layer
		/*! \note The final layer value is inherited from the parent if the node layer is 0. */
		inline std::uint16_t absLayer() const {
			return absLayer_;
		}
		/// Sets the node rendering layer
		/*! \note The lowest value (bottom) is 0 and the highest one (top) is 65535.
		 *  When the value is 0, the final layer value is inherited from the parent. */
		void setLayer(std::uint16_t layer) {
			layer_ = layer;
		}

		/// Gets the node world matrix
		inline const Matrix4x4f& worldMatrix() const {
			return worldMatrix_;
		}
		/// Sets the node world matrix (only useful when called inside `OnPostUpdate()`)
		void setWorldMatrix(const Matrix4x4f& worldMatrix);

		/// Gets the node local matrix
		inline const Matrix4x4f& localMatrix() const {
			return localMatrix_;
		}
		/// Sets the node local matrix
		void setLocalMatrix(const Matrix4x4f& localMatrix);

		/// Gets the delete children on destruction flag
		/*! If the flag is true the children are deleted upon node destruction. */
		inline bool deleteChildrenOnDestruction() const {
			return shouldDeleteChildrenOnDestruction_;
		}
		/// Sets the delete children on destruction flag
		inline void setDeleteChildrenOnDestruction(bool shouldDeleteChildrenOnDestruction) {
			shouldDeleteChildrenOnDestruction_ = shouldDeleteChildrenOnDestruction;
		}

		/// Returns the last frame in which any of the viewports have updtated this node
		inline std::uint32_t lastFrameUpdated() const {
			return lastFrameUpdated_;
		}

	protected:
		/// Bit positions inside the dirty bitset
		enum DirtyBitPositions
		{
			// Reset by `SceneNode::transform()`
			TransformationBit = 0,
			ColorBit = 1,
			// Reset by `BaseSprite::updateRenderCommand()`
			SizeBit = 2,
			TextureBit = 3,
			// Reset by `DrawableNode::updateCulling()`
			AabbBit = 4,
			// Reset by `DrawableNode::draw()` (for non culled nodes) and by `SceneNode::update()`
			// They are both used for OpenGL GPU uploading.
			TransformationUploadBit = 5,
			ColorUploadBit = 6,
		};

		/// A pointer to the parent node
		SceneNode* parent_;
		/// The array of child nodes
		SmallVector<SceneNode*, 0> children_;
		/// The order index of this node among its siblings
		/*! \note The index is cached here to make siblings reordering methods faster */
		std::uint32_t childOrderIndex_;

		bool updateEnabled_;
		bool drawEnabled_;
		/// When enabled the visit order is used to resolve the drawing order of same layer nodes
		/*! \note This flag cannot be changed by the user, it is derived from the parent and this node states */
		bool withVisitOrder_;

		/// A flag indicating whether the destructor should also delete all children
		bool shouldDeleteChildrenOnDestruction_;

		/// The visit order state of this node
		enum VisitOrderState visitOrderState_;
		/// The visit order index of this node
		std::uint16_t visitOrderIndex_;

		/// The node rendering layer
		/*! Even if the base scene node is not always drawable, it carries
		 *  layer information to easily pass that information to its children. */
		std::uint16_t layer_;

		/// The node relative position
		Vector2f position_;
		/// The anchor point for transformations, in pixels
		/// \note The default point is the center
		Vector2f anchorPoint_;
		/// Horizontal and vertical scale factors for node size
		Vector2f scaleFactor_;
		/// Clock-wise node rotation in radians
		float rotation_;

		/// Node color for transparency and translucency
		/*! Even if the base scene node is not always drawable, it carries
		 *  color information to easily pass that information to its children. */
		Colorf color_;

		/// Absolute position as calculated by the `transform()` function
		Vector2f absPosition_;
		/// Absolute horizontal and vertical scale factors as calculated by the `transform()` function
		Vector2f absScaleFactor_;
		/// Absolute node rotation as calculated by the `transform()` function
		float absRotation_;

		/// Absolute node color as calculated by the `transform()` function
		Colorf absColor_;

		/// Absolute node rendering layer as calculated by the `transform()` function
		std::uint16_t absLayer_;

		/// Bitset that stores the various dirty states bits
		BitSet<std::uint8_t> dirtyBits_;

		/// World transformation matrix (calculated from local and parent's world)
		Matrix4x4f worldMatrix_;
		/// Local transformation matrix
		Matrix4x4f localMatrix_;

		/// The last frame any viewport updated this node
		std::uint32_t lastFrameUpdated_;

		/// Protected copy constructor used to clone objects
		SceneNode(const SceneNode& other);

		/// Swaps the child pointer of a parent when moving an object
		void swapChildPointer(SceneNode* first, SceneNode* second);

		virtual void transform();
	};

	inline const SmallVectorImpl<const SceneNode*>& SceneNode::children() const
	{
		return reinterpret_cast<const SmallVectorImpl<const SceneNode*>&>(children_);
	}

	inline void SceneNode::setEnabled(bool enabled)
	{
		updateEnabled_ = enabled;
		drawEnabled_ = enabled;
	}

	inline void SceneNode::setPosition(float x, float y)
	{
		position_.Set(x, y);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setPosition(Vector2f position)
	{
		position_ = position;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setPositionX(float x)
	{
		position_.X = x;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setPositionY(float y)
	{
		position_.Y = y;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::move(float x, float y)
	{
		position_.X += x;
		position_.Y += y;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::move(Vector2f position)
	{
		position_ += position;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::moveX(float x)
	{
		position_.X += x;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::moveY(float y)
	{
		position_.Y += y;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setAbsAnchorPoint(float x, float y)
	{
		anchorPoint_.Set(x, y);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setAbsAnchorPoint(Vector2f point)
	{
		anchorPoint_ = point;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setScale(float scaleFactor)
	{
		scaleFactor_.Set(scaleFactor, scaleFactor);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setScale(float scaleFactorX, float scaleFactorY)
	{
		scaleFactor_.Set(scaleFactorX, scaleFactorY);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setScale(Vector2f scaleFactor)
	{
		scaleFactor_ = scaleFactor;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setRotation(float rotation)
	{
		rotation_ = fmodf(rotation, fRadAngle360);
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setColor(const Color& color)
	{
		color_ = color;
		dirtyBits_.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::setColor(const Colorf& color)
	{
		color_ = color;
		dirtyBits_.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::setAlpha(std::uint8_t alpha)
	{
		color_.SetAlpha(alpha / 255.0f);
		dirtyBits_.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::setAlphaF(float alpha)
	{
		color_.SetAlpha(alpha);
		dirtyBits_.set(DirtyBitPositions::ColorBit);
	}

	inline void SceneNode::setWorldMatrix(const Matrix4x4f& worldMatrix)
	{
		worldMatrix_ = worldMatrix;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	inline void SceneNode::setLocalMatrix(const Matrix4x4f& localMatrix)
	{
		localMatrix_ = localMatrix;
		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}
}
